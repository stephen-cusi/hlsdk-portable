/***
*
*	XGLua — CLuaWeapon: GMod-style scripted weapons (SWEP) for HL1
*
*	Design (per doc/XGLua_Design.md section 9.5, P1 server-authoritative):
*	- SWEPs run on the server only; visuals via pev->weaponanim / sounds
*	- One generic C++ class, all virtuals forwarded to the Lua SWEP table
*	- Lua weapons are created via CREATE_ENTITY() + placement new, bypassing
*	  the engine's export-symbol lookup (class names unknown at compile time)
*
****/

#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

#include "xglua.h"
#include "lua/xglua_weapon_anim.h"
#include "xglua_swep_bridge.h"
#include "xglua_swep_order.h"
#include "xglua_restore_map.h"
#include "xglua_restore_inventory.h"
#include "com_model.h"
#include "weapons.h"
#include "player.h"
#include "xglua_ballistics.h"
#include "xglua_secondary_ammo.h"
#include "lua/xglua_cl_swep_meta.h"
#include "xglua_sound_precache.h"
#include "monsters.h" // EjectBrass
extern "C" {
#include "lauxlib.h"
}

extern int giAmmoIndex;
extern void AddAmmoNameToAmmoRegistry( const char *szAmmoname );	// weapons.cpp

//===========================================================================
// SWEP C++ cache — read once from the Lua SWEP table at startup
//===========================================================================

struct XGLuaSWEPInfo
{
	char classname[64];
	char viewmodel[128];
	char worldmodel[128];
	char playermodel[128];
	char extraModels[8][128];
	int numExtraModels;
	char animextension[32];
	int deploysequence;
	char ammo1[32];
	char ammo2[32];
	int defaultammo2;
	int maxammo2;
	char hudname[64];	// name reported to the client HUD (must be a known classname)
	int id;
	bool primaryAutomatic;
	bool secondaryAutomatic;
	int clipsize;
	int defaultclip;
	int maxammo;
	int slot;
	int slotpos;
	int weight;
	ItemInfo info;		// filled from the fields above
};

#define XGLUA_MAX_SWEP 64
static XGLuaSWEPInfo g_SWEPs[XGLUA_MAX_SWEP];
static int g_nSWEPs = 0;
static XGLuaCLSWEPRegistry g_SVSWEPMeta;

const XGLuaCLSWEPRegistry &XGLua_GetServerSWEPMeta( void )
{
	return g_SVSWEPMeta;
}

static char g_XGLuaSounds[XGLUA_SOUND_TOTAL][XGLUA_SOUND_PATH_BYTES];
static int g_nXGLuaSounds = 0;

static bool XGLua_SoundExists( const char *name )
{
	char path[XGLUA_SOUND_PATH_BYTES + 6];
	snprintf( path, sizeof( path ), "sound/%s", name );
	if( g_engfuncs.pfnGetFileSize ) return g_engfuncs.pfnGetFileSize( path ) > 0;
	if( !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile ) return false;
	int length = 0;
	byte *data = LOAD_FILE_FOR_ME( path, &length );
	if( !data ) return false;
	FREE_FILE( data );
	return length > 0;
}

static bool XGLua_ModelExists( const char *path )
{
	if( !path || !path[0] ) return true;
	if( strncmp( path, "models/", 7 ) || strstr( path, ".." ) || strchr( path, '\\' ) ) return false;
	if( g_engfuncs.pfnGetFileSize ) return g_engfuncs.pfnGetFileSize( path ) > 0;
	if( !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile ) return false;
	int size = 0;
	byte *data = LOAD_FILE_FOR_ME( path, &size );
	if( !data ) return false;
	FREE_FILE( data );
	return size > 0;
}

static XGLuaSWEPInfo *XGLua_FindSWEP( const char *classname )
{
	for( int i = 0; i < g_nSWEPs; i++ )
	{
		if( !strcmp( g_SWEPs[i].classname, classname ) )
			return &g_SWEPs[i];
	}
	return NULL;
}

// Track concrete Lua weapon instances independently of their registered class:
// during save restoration the VM can be absent and scripts can be removed.
struct XGLuaTrackedWeapon { edict_t *edict; int serial; void *object; };
static std::vector<XGLuaTrackedWeapon> g_TrackedWeapons;
static std::vector<XGLuaRestoreMarker<CBasePlayer, edict_t> > g_RestoredPlayers;
void XGLua_MarkRestoredPlayer( CBasePlayer *player )
{
    if( !player || !player->edict() ) return;
    XGLuaRestoreMarker<CBasePlayer, edict_t> value = { player, player->edict(), player->edict()->serialnumber };
    for( size_t i=0; i<g_RestoredPlayers.size(); ++i )
        if( g_RestoredPlayers[i].edict==value.edict ) { g_RestoredPlayers[i]=value; return; }
    g_RestoredPlayers.push_back( value );
}
static bool XGLua_IsRestoredPlayer( CBasePlayer *player )
{
    if( !player || !player->edict() || player->edict()->free ) return false;
    for( size_t i=0; i<g_RestoredPlayers.size(); ++i )
        if( XGLua_MarkerMatches( g_RestoredPlayers[i], player, player->edict(), player->edict()->serialnumber, true ) ) return true;
    return false;
}
static void XGLua_TrackWeapon( edict_t *edict, void *object )
{
    XGLuaTrackedWeapon value = { edict, edict->serialnumber, object };
    for( size_t i = 0; i < g_TrackedWeapons.size(); ++i )
        if( g_TrackedWeapons[i].edict == edict ) { g_TrackedWeapons[i] = value; return; }
    g_TrackedWeapons.push_back( value );
}
bool XGLua_IsTrackedWeapon( const edict_t *edict )
{
    if( !edict || edict->free ) return false;
    for( size_t i = 0; i < g_TrackedWeapons.size(); ++i )
        if( g_TrackedWeapons[i].edict == edict && g_TrackedWeapons[i].serial == edict->serialnumber &&
            g_TrackedWeapons[i].object == edict->pvPrivateData ) return true;
    return false;
}
void XGLua_ClearWeaponTracking() { g_TrackedWeapons.clear(); g_RestoredPlayers.clear(); g_nSWEPs = 0; g_nXGLuaSounds = 0; }

//===========================================================================
// CLuaWeapon
//===========================================================================

class CLuaWeapon : public CBasePlayerWeapon
{
public:
	char m_szLuaClass[64];
	int m_iDefaultSecondaryAmmo;
	float m_xglSavedAttackTimes[3];
	bool m_xglRestoreMissing;
	static TYPEDESCRIPTION m_xglSaveData[];
	int Save( CSave &save );
	int Restore( CRestore &restore );

	void Spawn( void );
	void Precache( void );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void Reload( void );
	void WeaponIdle( void );
	void ItemPostFrame( void );
	BOOL CanDeploy( void );
	int GetItemInfo( ItemInfo *p );
	int iItemSlot( void );
	int SecondaryAmmoIndex( void ) { return m_iSecondaryAmmoType; }
	int ExtractAmmo( CBasePlayerWeapon *pWeapon );
	int AddDuplicate( CBasePlayerItem *pOriginal );

	void InitFromCache( const char *classname );
};

// The engine creates restore table entities before the world or Lua VM.
// This factory must never consult the Lua registry or call Spawn.
extern "C" EXPORT void xglua_swep_restore( entvars_t *pev );
void xglua_swep_restore( entvars_t *pev )
{
    CLuaWeapon *weapon = new( pev ) CLuaWeapon();
    weapon->pev = pev;
    weapon->m_szLuaClass[0] = 0;
    weapon->m_xglRestoreMissing = true;
    XGLua_TrackWeapon( ENT( pev ), weapon );
}

TYPEDESCRIPTION CLuaWeapon::m_xglSaveData[] = {
    DEFINE_FIELD( CLuaWeapon, m_iDefaultSecondaryAmmo, FIELD_INTEGER ),
    DEFINE_ARRAY( CLuaWeapon, m_xglSavedAttackTimes, FIELD_TIME, 3 ),
};
int CLuaWeapon::Save( CSave &save )
{
    m_xglSavedAttackTimes[0] = m_flNextPrimaryAttack;
    m_xglSavedAttackTimes[1] = m_flNextSecondaryAttack;
    m_xglSavedAttackTimes[2] = m_flTimeWeaponIdle;
    if( !CBasePlayerWeapon::Save( save ) ) return 0;
    return save.WriteFields( "XGLuaWeapon", this, m_xglSaveData, ARRAYSIZE( m_xglSaveData ) );
}
int CLuaWeapon::Restore( CRestore &restore )
{
    if( !CBasePlayerWeapon::Restore( restore ) ) return 0;
    const bool saved = restore.ReadFields( "XGLuaWeapon", this, m_xglSaveData, ARRAYSIZE( m_xglSaveData ) ) != 0;
    if( saved ) {
        m_flNextPrimaryAttack = m_xglSavedAttackTimes[0];
        m_flNextSecondaryAttack = m_xglSavedAttackTimes[1];
        m_flTimeWeaponIdle = m_xglSavedAttackTimes[2];
    }
    const char *classname = STRING( pev->classname );
    strncpy( m_szLuaClass, classname ? classname : "", sizeof( m_szLuaClass ) - 1 );
    m_szLuaClass[sizeof( m_szLuaClass ) - 1] = 0;
    XGLuaSWEPInfo *swep = g_xglua_L && XGLua_IsLuaWeapon( m_szLuaClass ) ? XGLua_FindSWEP( m_szLuaClass ) : NULL;
    m_xglRestoreMissing = !swep;
    m_iId = swep ? swep->id : 0;
    // Restored ground and boxed weapons may later be picked up without going
    // through player reconciliation. Their saved ammo indices are map-local.
    m_iPrimaryAmmoType = swep && swep->info.pszAmmo1 ? CBasePlayer::GetAmmoIndex( swep->info.pszAmmo1 ) : -1;
    m_iSecondaryAmmoType = swep && swep->info.pszAmmo2 ? CBasePlayer::GetAmmoIndex( swep->info.pszAmmo2 ) : -1;
    m_fInReload = FALSE;
    m_iClientClip = -1;
    m_iClientWeaponState = -1;
    return 1;
}

static int XGLua_PushWeaponEntity( lua_State *L, void *ctx )
{
	XGLua_PushEntity( L, ( (CLuaWeapon *)ctx )->edict() );
	return 1;
}

static void CallWeaponMethod( CLuaWeapon *w, const char *method )
{
	lua_State *L = g_xglua_L;
	if( !L || !w )
		return;

	// Protected dispatch: the inherited lookup (SWEP __index runs the Base
	// resolver in weapons.lua), the entity handle push, and the call share
	// one lua_pcall frame - no error path can escape into the engine.
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "SWEP:%s", method );
	XGLua_CallSWEPMethod( L, w->m_szLuaClass, method,
		XGLua_PushWeaponEntity, w, XGLua_ReportError, szCtx );
}

void CLuaWeapon::InitFromCache( const char *classname )
{
	strncpy( m_szLuaClass, classname, sizeof( m_szLuaClass ) - 1 );
	m_szLuaClass[sizeof( m_szLuaClass ) - 1] = '\0';

	XGLuaSWEPInfo *swep = XGLua_FindSWEP( classname );
	m_iId = swep ? swep->id : 0;
	// keep m_iClip == 0 so the base ExtractAmmo clip-fill branch runs;
	// a SWEP without Primary.DefaultClip still gets a full clip
	m_iDefaultAmmo = swep ? swep->defaultclip : 0;
	m_iDefaultSecondaryAmmo = swep ? swep->defaultammo2 : 0;
	m_iClip = ( swep && swep->clipsize < 0 ) ? WEAPON_NOCLIP : 0;
	m_fInReload = 0;
	m_fFireOnEmpty = FALSE;
	m_iPlayEmptySound = 1;
	m_xglRestoreMissing = false;
}


int CLuaWeapon::ExtractAmmo( CBasePlayerWeapon *target )
{
	int result = CBasePlayerWeapon::ExtractAmmo( target );
	if( pszAmmo2() && m_iDefaultSecondaryAmmo > 0 )
		result |= target->AddSecondaryAmmo( m_iDefaultSecondaryAmmo, (char *)pszAmmo2(), iMaxAmmo2() );
	m_iDefaultSecondaryAmmo = 0;
	return result;
}

int CLuaWeapon::AddDuplicate( CBasePlayerItem *original )
{
	// Native AddDuplicate calls virtual ExtractAmmo only if primary default ammo
	// is nonzero. Supply secondary ammo on the clip-only path as well.
	int result = XGLua_DuplicateAmmo( m_iDefaultAmmo != 0, pszAmmo1() != NULL && m_iClip > 0,
		[&]() { return CBasePlayerWeapon::AddDuplicate( original ); },
		[&]() {
			if( !pszAmmo2() || m_iDefaultSecondaryAmmo <= 0 ) return 0;
			return (int)((CBasePlayerWeapon *)original)->AddSecondaryAmmo( m_iDefaultSecondaryAmmo, (char *)pszAmmo2(), iMaxAmmo2() );
		} );
	m_iDefaultSecondaryAmmo = 0;
	return result;
}

void CLuaWeapon::Precache( void )
{
	// models are precached at SWEP registration (server spawn phase) —
	// runtime precache is illegal in GoldSrc; this stays a no-op marker
}

void CLuaWeapon::Spawn( void )
{
	Precache();
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	m_iId = swep ? swep->id : 0;
	if( swep && swep->worldmodel[0] )
		SET_MODEL( ENT( pev ), swep->worldmodel );

	pev->movetype = MOVETYPE_NONE;
	FallInit();	// default size + materialize think (CBasePlayerItem)

	CallWeaponMethod( this, "Initialize" );
}

BOOL CLuaWeapon::Deploy( void )
{
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	BOOL ok = TRUE;
	if( swep && swep->viewmodel[0] )
	{
		const char *playerModel = swep->playermodel[0] ? swep->playermodel : swep->worldmodel;
		ok = DefaultDeploy( swep->viewmodel, playerModel, swep->deploysequence, swep->animextension );
		// CLuaWeapon does not decrement its own timers, even in CLIENT_WEAPONS builds.
		if( ok ) m_flTimeWeaponIdle = XGLua_WeaponIdleDeadline( gpGlobals->time, 1.0f );
	}
	CallWeaponMethod( this, "Deploy" );
	return ok;
}

void CLuaWeapon::Holster( int skiplocal )
{
	CallWeaponMethod( this, "Holster" );
	CBasePlayerWeapon::Holster( skiplocal );
}

void CLuaWeapon::PrimaryAttack( void )
{
	CallWeaponMethod( this, "PrimaryAttack" );
}

void CLuaWeapon::SecondaryAttack( void )
{
	CallWeaponMethod( this, "SecondaryAttack" );
}

void CLuaWeapon::Reload( void )
{
	CallWeaponMethod( this, "Reload" );
}

void CLuaWeapon::WeaponIdle( void )
{
	CallWeaponMethod( this, "WeaponIdle" );
}

BOOL CLuaWeapon::CanDeploy( void )
{
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	if( swep && swep->clipsize > 0 )
	{
		// clip weapon: deployable if clip or reserve ammo available
		if( m_iClip > 0 )
			return TRUE;
		if( m_pPlayer && m_iPrimaryAmmoType >= 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
			return TRUE;
	}
	return CBasePlayerWeapon::CanDeploy();
}

void CLuaWeapon::ItemPostFrame( void )
{
	if( !m_pPlayer ) return;

	// 1. Complete a pending reload when the attack/reload timer expires.
	if( m_fInReload )
	{
		if( m_pPlayer->m_flNextAttack > UTIL_WeaponTimeBase() )
			return;

		int n = ( m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS ) ?
			Q_min( iMaxClip() - m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] ) : 0;
		if( n > 0 )
			m_iClip += n;
		if( m_iPrimaryAmmoType >= 0 && n > 0 )
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= n;
		m_pPlayer->TabulateAmmo();
		m_fInReload = FALSE;
	}

	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	if( !swep ) return;

	int buttons = m_pPlayer->pev->button;
	int pressed = m_pPlayer->m_afButtonPressed;

	// 2. Manual reload request (only when not already reloading)
	if( ( buttons & IN_RELOAD ) && !m_fInReload )
	{
		Reload();
		return;
	}

	// 3. Block primary/secondary fire and idle override while reloading
	if( m_fInReload )
		return;

	if( ( buttons & IN_ATTACK2 ) && ( swep->secondaryAutomatic || ( pressed & IN_ATTACK2 ) ) && gpGlobals->time >= m_flNextSecondaryAttack )
		SecondaryAttack();
	else if( ( buttons & IN_ATTACK ) && ( swep->primaryAutomatic || ( pressed & IN_ATTACK ) ) && gpGlobals->time >= m_flNextPrimaryAttack )
		PrimaryAttack();
	else if( !( buttons & ( IN_ATTACK | IN_ATTACK2 ) ) )
	{
		ResetEmptySound();
		WeaponIdle();
	}
}

int CLuaWeapon::GetItemInfo( ItemInfo *p )
{
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	if( !swep )
		return 0;
	*p = swep->info;
	p->pszName = swep->classname;
	return 1;
}

int CLuaWeapon::iItemSlot( void )
{
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	return swep ? swep->slot : 2;
}

//===========================================================================
// creation / give
//===========================================================================

CBasePlayerItem *XGLua_SpawnLuaWeapon( const char *classname, const Vector &origin )
{
	if( !XGLua_IsLuaWeapon( classname ) )
		return NULL;

	edict_t *pent = CREATE_ENTITY();
	if( FNullEnt( pent ) )
		return NULL;

	entvars_t *pev = VARS( pent );
	pev->classname = MAKE_STRING( classname );
	pev->origin = origin;

	// placement new — mirrors GetClassPtr<T>() (cbase.h), private data
	// is allocated by the engine right after the entvars block
	CLuaWeapon *w = new( pev ) CLuaWeapon();
	w->pev = pev;
	XGLua_TrackWeapon( pent, w );
	w->InitFromCache( classname );

	return w;
}

CBasePlayerItem *XGLua_GiveLuaWeapon( CBasePlayer *pPlayer, const char *classname )
{
	CBasePlayerItem *pItem = XGLua_SpawnLuaWeapon( classname, pPlayer->pev->origin );
	if( !pItem )
		return NULL;

	edict_t *pent = pItem->edict();
	pent->v.spawnflags |= SF_NORESPAWN;

	// mirror CBasePlayer::GiveNamedItem, then auto-equip: the client HUD
	// doesn't know "weapon_grenadepistol", so slot-based selection can't
	// reach it — server-side switch is the v1 answer (design doc 9.5 P1)
	DispatchSpawn( pent );
	DispatchTouch( pent, ENT( pPlayer->pev ) );
	// DispatchTouch may have rejected this item or consumed it as a duplicate.
	// Find the actual owned object before selecting or returning it.
	for( int slot = 0; slot < MAX_ITEM_TYPES; ++slot )
		for( CBasePlayerItem *owned = pPlayer->m_rgpPlayerItems[slot]; owned; owned = owned->m_pNext )
			if( FClassnameIs( owned->pev, classname ) )
			{
				pPlayer->SelectItem( classname );
				return owned;
			}
	return NULL;
}

// Called only after the game-rules living-player check. Native rules normally
// reject duplicate weapons when their primary ammo is full, even if secondary
// ammo in the pickup could be used.
BOOL XGLua_CanPickupSecondary( CBasePlayer *player, CBasePlayerItem *item )
{
	if( !g_xglua_L || !player || !item || !item->pev ) return FALSE;
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( STRING( item->pev->classname ) );
	if( !swep || item->m_iId != swep->id || !swep->info.pszAmmo2 || !player->HasPlayerItem( item ) ) return FALSE;
	CLuaWeapon *weapon = (CLuaWeapon *)item;
	if( weapon->m_iDefaultSecondaryAmmo <= 0 ) return FALSE;
	int index = player->GetAmmoIndex( swep->info.pszAmmo2 );
	int current = index > 0 && index < MAX_AMMO_SLOTS ? player->AmmoInventory( index ) : 0;
	return XGLua_SecondaryPickupAllowed( player->pev->deadflag == DEAD_NO, true, true,
		weapon->m_iDefaultSecondaryAmmo, index, MAX_AMMO_SLOTS, current, swep->info.iMaxAmmo2 );
}

//===========================================================================
// Lua-side weapon methods (live on the shared Entity metatable)
//===========================================================================

static XGLuaSWEPInfo *SWEPInfoFromLua( lua_State *L, int idx )
{
	edict_t *pent = XGLua_CheckEntity( L, idx );
	CBaseEntity *pEnt = CBaseEntity::Instance( pent );
	// classname registered <=> created by XGLua_SpawnLuaWeapon ⇒ instance is
	// CLuaWeapon (no dynamic_cast: GCC builds compile with -fno-rtti)
	XGLuaSWEPInfo *swep = pEnt ? XGLua_FindSWEP( STRING( pEnt->pev->classname ) ) : NULL;
	if( !swep )
		luaL_error( L, "Tried to use a non-weapon entity as a Weapon!" );

	return swep;
}

static CLuaWeapon *WeaponFromLua( lua_State *L, int idx )
{
	edict_t *pent = XGLua_CheckEntity( L, idx );
	CBaseEntity *pEnt = CBaseEntity::Instance( pent );
	if( !pEnt || !XGLua_FindSWEP( STRING( pEnt->pev->classname ) ) )
		luaL_error( L, "Tried to use a non-weapon entity as a Weapon!" );
	return (CLuaWeapon *)pEnt;
}

static int Wep_SetNextPrimaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	double t = luaL_checknumber( L, 2 );
	w->m_flNextPrimaryAttack = (float)t;
	return 0;
}

static int Wep_GetNextPrimaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushnumber( L, w->m_flNextPrimaryAttack );
	return 1;
}

static int Wep_SetNextSecondaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	double t = luaL_checknumber( L, 2 );
	w->m_flNextSecondaryAttack = (float)t;
	return 0;
}

static int Wep_GetNextSecondaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushnumber( L, w->m_flNextSecondaryAttack );
	return 1;
}

static int Wep_Clip1( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushnumber( L, w->m_iClip );
	return 1;
}

static int Wep_SetClip1( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	w->m_iClip = (int)luaL_checknumber( L, 2 );
	return 0;
}

static int Wep_TakePrimaryAmmo( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int n = (int)luaL_checknumber( L, 2 );
	if( n <= 0 ) return 0;
	if( w->iMaxClip() != WEAPON_NOCLIP )
		w->m_iClip = Q_max( 0, w->m_iClip - n );
	else if( w->m_pPlayer && w->PrimaryAmmoIndex() >= 0 )
		w->m_pPlayer->m_rgAmmo[w->PrimaryAmmoIndex()] = Q_max( 0, w->m_pPlayer->m_rgAmmo[w->PrimaryAmmoIndex()] - n );
	return 0;
}


static int Wep_TakeSecondaryAmmo( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int n = (int)luaL_checknumber( L, 2 );
	int index = w->SecondaryAmmoIndex();
	if( n > 0 && w->m_pPlayer && w->pszAmmo2() && index > 0 && index < MAX_AMMO_SLOTS )
		w->m_pPlayer->m_rgAmmo[index] = XGLua_SecondaryRemaining( w->m_pPlayer->m_rgAmmo[index], n );
	return 0;
}

static int Wep_SetWeaponIdleTime( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	double time = luaL_checknumber( L, 2 );
	if( !std::isfinite( time ) || time < -3.4e38 || time > 3.4e38 ) return luaL_argerror( L, 2, "time must fit a finite float" );
	w->m_flTimeWeaponIdle = (float)time;
	return 0;
}

static int Wep_GetWeaponIdleTime( lua_State *L )
{
	lua_pushnumber( L, WeaponFromLua( L, 1 )->m_flTimeWeaponIdle );
	return 1;
}

static int Wep_MuzzleFlash( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	if( !w->m_pPlayer ) return luaL_error( L, "MuzzleFlash requires a player-owned weapon" );
	w->m_pPlayer->pev->effects = (int)w->m_pPlayer->pev->effects | EF_MUZZLEFLASH;
	return 0;
}

static int Wep_SendOwnerAttackAnimation( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	if( !w->m_pPlayer ) return luaL_error( L, "SendOwnerAttackAnimation requires a player-owned weapon" );
	w->m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	return 0;
}

static int Wep_DefaultReload( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int clip = (int)luaL_checknumber( L, 2 );
	int anim = (int)luaL_checknumber( L, 3 );
	float delay = (float)luaL_checknumber( L, 4 );
	int body = lua_isnoneornil( L, 5 ) ? w->pev->body : XGLua_CheckWeaponBody( L, 5 );
	if( !w->m_pPlayer || w->PrimaryAmmoIndex() < 0 || w->m_fInReload ||
		clip <= w->m_iClip || clip > w->iMaxClip() || delay < 0 )
	{ lua_pushboolean( L, false ); return 1; }
	w->pev->body = body;
	BOOL result = w->DefaultReload( clip, anim, delay, body );
	if( result ) w->m_flTimeWeaponIdle = gpGlobals->time + delay + 0.25f;
	lua_pushboolean( L, result );
	return 1;
}

// Server-authoritative hitscan; the script is responsible for ammo and animations.
static int Wep_ShootBullet( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	double damage = luaL_checknumber( L, 2 );
	double count = luaL_checknumber( L, 3 );
	Vector spread = XGLua_CheckVector( L, 4 );
	if( !XGLua_ValidBulletDamage( damage ) )
		return luaL_argerror( L, 2, "damage must be an integer from 1 to 1000" );
	if( !XGLua_ValidBulletCount( count ) )
		return luaL_argerror( L, 3, "bullet count must be an integer from 1 to 32" );
	if( !XGLua_ValidBulletSpread( spread ) )
		return luaL_argerror( L, 4, "spread must be finite and x/y between 0 and 1" );
	CBasePlayer *player = w->m_pPlayer;
	if( !player )
		return luaL_error( L, "ShootBullet requires a player-owned weapon" );
	// FireBulletsPlayer requires a current basis and a player as inflictor.
	XGLua_FirePlayerBullets( player, (int)damage, (int)count, spread );
	return 0;
}

static int Wep_SendWeaponAnim( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int seq = (int)luaL_checknumber( L, 2 );
	if( !w->m_pPlayer ) return luaL_error( L, "SendWeaponAnim requires a player-owned weapon" );
	// GoldSrc transmits pev->body, not the body argument, from this path.
	if( !lua_isnoneornil( L, 3 ) ) w->pev->body = XGLua_CheckWeaponBody( L, 3 );
	w->SendWeaponAnim( seq, 0, w->pev->body );
	return 0;
}

static int Wep_PlayEmptySound( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushboolean( L, w->PlayEmptySound() );
	return 1;
}
static int Wep_SetBody( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	w->pev->body = XGLua_CheckWeaponBody( L, 2 );
	return 0;
}

static int Wep_GetBody( lua_State *L )
{
	lua_pushinteger( L, WeaponFromLua( L, 1 )->pev->body );
	return 1;
}

static int Wep_GetNextAttackDelay( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	float delay = (float)luaL_checknumber( L, 2 );
	if( !std::isfinite( delay ) || delay < 0.0f )
		return luaL_argerror( L, 2, "delay must be a non-negative finite number" );
	lua_pushnumber( L, w->GetNextAttackDelay( delay ) );
	return 1;
}

static int Wep_IsReloading( lua_State *L )
{
	lua_pushboolean( L, WeaponFromLua( L, 1 )->m_fInReload != FALSE );
	return 1;
}

static int Wep_SetNextAttack( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	float delay = (float)luaL_checknumber( L, 2 );
	if( !std::isfinite( delay ) || delay < 0.0f )
		return luaL_argerror( L, 2, "delay must be a non-negative finite number" );
	if( !w->m_pPlayer )
		return luaL_error( L, "SetNextAttack requires a player-owned weapon" );
	w->m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + delay;
	return 0;
}

// EjectBrass( modelPath, rightVel, upVel [, fwdOff, rightOff, upOff, soundType] )
static int Wep_EjectBrass( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	const char *model = luaL_checkstring( L, 2 );
	float rightVel = (float)luaL_checknumber( L, 3 );
	float upVel    = (float)luaL_checknumber( L, 4 );
	float fwdOff   = (float)luaL_optnumber( L, 5, 16.0 );
	float rightOff = (float)luaL_optnumber( L, 6, 8.0 );
	float upOff    = (float)luaL_optnumber( L, 7, -6.0 );
	int soundType  = (int)luaL_optinteger( L, 8, TE_BOUNCE_SHELL );

	if( !w->m_pPlayer )
		return luaL_error( L, "EjectBrass requires a player-owned weapon" );
	if( !XGLua_ModelExists( model ) )
		return luaL_argerror( L, 2, "invalid or missing brass model path" );

	int modelIndex = MODEL_INDEX( model );
	if( modelIndex <= 0 )
		return 0;

	CBasePlayer *p = w->m_pPlayer;
	UTIL_MakeVectors( p->pev->v_angle + p->pev->punchangle );
	Vector src = p->GetGunPosition() + gpGlobals->v_forward * fwdOff + gpGlobals->v_right * rightOff + gpGlobals->v_up * upOff;
	Vector vel = p->pev->velocity + gpGlobals->v_right * rightVel + gpGlobals->v_up * upVel + gpGlobals->v_forward * RANDOM_FLOAT( 15.0f, 35.0f );
	EjectBrass( src, vel, p->pev->angles.y, modelIndex, soundType );
	return 0;
}

//===========================================================================
// registration
//===========================================================================

void XGLua_RegisterWeaponLibs( lua_State *L )
{
	// weapon methods join the shared Entity metatable
	luaL_getmetatable( L, "XGLua_Entity" );
	if( lua_istable( L, -1 ) )
	{
		const luaL_Reg wepmeta[] =
		{
			{ "SetNextPrimaryFire", Wep_SetNextPrimaryFire },
			{ "GetNextPrimaryFire", Wep_GetNextPrimaryFire },
			{ "SetNextSecondaryFire", Wep_SetNextSecondaryFire },
			{ "GetNextSecondaryFire", Wep_GetNextSecondaryFire },
			{ "Clip1", Wep_Clip1 },
			{ "SetClip1", Wep_SetClip1 },
			{ "TakePrimaryAmmo", Wep_TakePrimaryAmmo },
			{ "TakeSecondaryAmmo", Wep_TakeSecondaryAmmo },
			{ "SetWeaponIdleTime", Wep_SetWeaponIdleTime },
			{ "GetWeaponIdleTime", Wep_GetWeaponIdleTime },
			{ "MuzzleFlash", Wep_MuzzleFlash },
			{ "SendOwnerAttackAnimation", Wep_SendOwnerAttackAnimation },
			{ "ShootBullet", Wep_ShootBullet },
			{ "DefaultReload", Wep_DefaultReload },
			{ "SendWeaponAnim", Wep_SendWeaponAnim },
			{ "PlayEmptySound", Wep_PlayEmptySound },
			{ "SetBody", Wep_SetBody },
			{ "GetBody", Wep_GetBody },
			{ "GetNextAttackDelay", Wep_GetNextAttackDelay },
			{ "IsReloading", Wep_IsReloading },
			{ "SetNextAttack", Wep_SetNextAttack },
			{ "EjectBrass", Wep_EjectBrass },
			{ NULL, NULL }
		};
		for( const luaL_Reg *f = wepmeta; f->name; f++ )
		{
			lua_pushcfunction( L, f->func );
			lua_setfield( L, -2, f->name );
		}
	}
	lua_pop( L, 1 );
}

// Field reads for one SWEP run in their own protected call: a raising
// __index (the Base inheritance resolver in weapons.lua) skips only this
// weapon instead of aborting the whole registration pass.
struct XGLua_SWEPFieldCtx
{
	int tableRef;		// temporary LUA_REGISTRYINDEX reference
	const char *classname;
	XGLuaSWEPInfo *slot;
	char ( *sounds )[XGLUA_SOUND_PATH_BYTES];
	int *soundCount;
	bool soundsValid;
	bool abstract;
	bool explicitSecondaryMax;
	bool validSecondary;
	bool explicitPrimaryMax;
	bool validPrimary;
};

static char g_szSWEPFieldError[256];
static void XGLua_SWEPFieldError( lua_State *L, const char *context )
{
	// runs after the protected call returned: strings only, no allocation
	snprintf( g_szSWEPFieldError, sizeof( g_szSWEPFieldError ), "%s", XGLua_ErrorText( L, -1 ) );
	lua_pop( L, 1 );
}

static int XGLua_SWEPFieldCore( lua_State *L, void *ud )
{
	XGLua_SWEPFieldCtx *fc = (XGLua_SWEPFieldCtx *)ud;
	XGLuaSWEPInfo *swep = fc->slot;
	XGLua_PushSWEPTableRef( L, fc->tableRef );
	const int table = lua_gettop( L );
	const char *classname = fc->classname;

	fc->abstract = XGLua_SWEPIsAbstract( L, table );

	fc->soundsValid = XGLua_ReadPrecacheSounds( L, table, fc->sounds, fc->soundCount );

	lua_getfield( L, table, "ViewModel" );
	if( lua_isstring( L, -1 ) )
		strncpy( swep->viewmodel, lua_tostring( L, -1 ), sizeof( swep->viewmodel ) - 1 );
	lua_pop( L, 1 );

	lua_getfield( L, table, "WorldModel" );
	if( lua_isstring( L, -1 ) )
		strncpy( swep->worldmodel, lua_tostring( L, -1 ), sizeof( swep->worldmodel ) - 1 );
	lua_pop( L, 1 );

	lua_getfield( L, table, "PlayerModel" );
	if( lua_isstring( L, -1 ) )
		strncpy( swep->playermodel, lua_tostring( L, -1 ), sizeof( swep->playermodel ) - 1 );
	lua_pop( L, 1 );

	lua_getfield( L, table, "DeploySequence" );
	if( lua_isnumber( L, -1 ) )
	{
		double sequence = lua_tonumber( L, -1 );
		if( std::isfinite( sequence ) && sequence >= 0 && sequence <= 255 && floor( sequence ) == sequence )
			swep->deploysequence = (int)sequence;
		else
			ALERT( at_console, "[XGLua] %s: invalid DeploySequence, using 7\n", classname );
	}
	lua_pop( L, 1 );

	lua_getfield( L, table, "AnimExtension" );
	if( lua_isstring( L, -1 ) )
	{
		const char *extension = lua_tostring( L, -1 );
		if( *extension && strlen( extension ) < sizeof( swep->animextension ) )
			strcpy( swep->animextension, extension );
		else
			ALERT( at_console, "[XGLua] %s: invalid AnimExtension, using onehanded\n", classname );
	}
	lua_pop( L, 1 );

	lua_getfield( L, table, "Slot" );
	swep->slot = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 2;
	swep->slot = Q_max( 0, Q_min( swep->slot, MAX_WEAPON_SLOTS - 1 ) );
	lua_pop( L, 1 );

	lua_getfield( L, table, "SlotPos" );
	swep->slotpos = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 1;
	swep->slotpos = Q_max( 0, Q_min( swep->slotpos, MAX_WEAPON_SLOTS - 1 ) );
	lua_pop( L, 1 );

	lua_getfield( L, table, "Weight" );
	if( lua_isnumber( L, -1 ) )
	{
		double weight = lua_tonumber( L, -1 );
		if( std::isfinite( weight ) && weight >= 0 && weight <= 255 && floor( weight ) == weight ) swep->weight = (int)weight;
	}
	lua_pop( L, 1 );

	fc->explicitSecondaryMax = false;
	fc->validSecondary = true;
	fc->explicitPrimaryMax = false;
	fc->validPrimary = true;
	swep->maxammo2 = 68;
	// SWEP.Primary sub-table
	lua_getfield( L, table, "Primary" );
	if( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Automatic" );
		swep->primaryAutomatic = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		lua_getfield( L, -1, "ClipSize" );
		swep->clipsize = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : -1;
		lua_pop( L, 1 );

		lua_getfield( L, -1, "DefaultClip" );
		swep->defaultclip = XGLua_InitialWeaponAmmo( lua_isnumber( L, -1 ), (int)lua_tonumber( L, -1 ), swep->clipsize );
		lua_pop( L, 1 );

		lua_getfield( L, -1, "Ammo" );
		if( lua_isstring( L, -1 ) )
			strncpy( swep->ammo1, lua_tostring( L, -1 ), sizeof( swep->ammo1 ) - 1 );
		lua_pop( L, 1 );
		lua_getfield( L, -1, "MaxAmmo" );
		if( !lua_isnil( L, -1 ) )
		{
			fc->explicitPrimaryMax = true;
			double amount = lua_tonumber( L, -1 );
			if( !lua_isnumber( L, -1 ) || !std::isfinite( amount ) || amount < 1 || amount > 254 || floor( amount ) != amount ) fc->validPrimary = false;
			else swep->maxammo = (int)amount;
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );

	lua_getfield( L, table, "Secondary" );
	if( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Automatic" );
		swep->secondaryAutomatic = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		lua_getfield( L, -1, "Ammo" );
		if( lua_isstring( L, -1 ) ) strncpy( swep->ammo2, lua_tostring( L, -1 ), sizeof( swep->ammo2 ) - 1 );
		lua_pop( L, 1 );
		lua_getfield( L, -1, "ClipSize" );
		if( !lua_isnil( L, -1 ) && ( !lua_isnumber( L, -1 ) || lua_tonumber( L, -1 ) != -1 ) ) fc->validSecondary = false;
		lua_pop( L, 1 );
		lua_getfield( L, -1, "DefaultAmmo" );
		if( !lua_isnil( L, -1 ) )
		{
			double amount = lua_tonumber( L, -1 );
			if( !lua_isnumber( L, -1 ) || !std::isfinite( amount ) || amount < 0 || amount > 254 || floor( amount ) != amount ) fc->validSecondary = false;
			else swep->defaultammo2 = (int)amount;
		}
		lua_pop( L, 1 );
		lua_getfield( L, -1, "MaxAmmo" );
		if( !lua_isnil( L, -1 ) )
		{
			fc->explicitSecondaryMax = true;
			double amount = lua_tonumber( L, -1 );
			if( !lua_isnumber( L, -1 ) || !std::isfinite( amount ) || amount < 1 || amount > 254 || floor( amount ) != amount ) fc->validSecondary = false;
			else swep->maxammo2 = (int)amount;
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
	swep->numExtraModels = 0;
	lua_getfield( L, table, "PrecacheModels" );
	if( lua_istable( L, -1 ) )
	{
		int n = (int)lua_objlen( L, -1 );
		for( int i = 1; i <= n && swep->numExtraModels < 8; ++i )
		{
			lua_rawgeti( L, -1, i );
			if( lua_isstring( L, -1 ) )
			{
				const char *m = lua_tostring( L, -1 );
				if( m && m[0] )
					strncpy( swep->extraModels[swep->numExtraModels++], m, 127 );
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );

	return 0;
}

struct XGLua_StoreSWEPTableCtx { int tableRef; const char *classname; };
static int XGLua_StoreSWEPTableCore( lua_State *L, void *ud )
{
	XGLua_StoreSWEPTableCtx *sc = (XGLua_StoreSWEPTableCtx *)ud;
	XGLua_PushSWEPTableRef( L, sc->tableRef );
	if( !XGLua_StoreSWEPTable( L, lua_gettop( L ), sc->classname ) )
	{
		lua_pushliteral( L, "failed to store SWEP table" );
		return lua_error( L );
	}
	return 0;
}

static int XGLua_BuildWeaponCacheCore( lua_State *L, void *ctx )
{
	(void)ctx;
	// iterate weapons.GetStored() (Lua side, filled by weapons.Register)
	lua_getglobal( L, "weapons" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return 0;
	}
	lua_getfield( L, -1, "GetStored" );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return 0;
	}
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		ALERT( at_console, "[XGLua] weapons.GetStored error: %s\n", XGLua_ErrorText( L, -1 ) );
		lua_pop( L, 1 );
		return 0;
	}

	// lua_next iteration order changes with each VM; IDs and ammo slots are
	// serialized as numeric indices, so register classnames in a stable order.
	const int count = XGLua_PushSortedWeaponNames( L, -1 );
	for( int nameIndex = 1; nameIndex <= count; ++nameIndex )
	{
		lua_rawgeti( L, -1, nameIndex );
		lua_pushvalue( L, -1 );
		lua_rawget( L, -4 );
		// [-2] key (classname), [-1] SWEP table
		if( lua_isstring( L, -2 ) && lua_istable( L, -1 ) )
		{
			const char *classname = lua_tostring( L, -2 );
			if( g_nSWEPs >= XGLUA_MAX_SWEP )
			{
				ALERT( at_console, "[XGLua] too many SWEPs, ignoring %s\n", classname );
				lua_pop( L, 2 );
				continue;
			}

			// Validate fields into a local slot first: nothing becomes visible
			// to the engine until every check below has passed, so an aborted
			// build never leaves a half-filled cache entry behind.
			XGLuaSWEPInfo local;
			memset( &local, 0, sizeof( XGLuaSWEPInfo ) );
			XGLuaSWEPInfo *swep = &local;
			swep->deploysequence = 7;
			strcpy( swep->animextension, "onehanded" );
			swep->maxammo = 68;
			char soundPaths[XGLUA_SOUND_PER_WEAPON][XGLUA_SOUND_PATH_BYTES] = {};
			int soundCount = 0;
			// Lua field reads are isolated per weapon: a raising __index (the
			// Base inheritance resolver in weapons.lua) skips only this SWEP
			// and registration continues with the remaining classes. The SWEP
			// lua_cpcall uses a fresh C-function stack frame. Pin this SWEP table
			// in the registry so nested protected readers can retrieve it safely.
			lua_pushvalue( L, -1 );
			const int tableRef = luaL_ref( L, LUA_REGISTRYINDEX );
			XGLua_SWEPFieldCtx fc = { tableRef, classname, swep, soundPaths, &soundCount, false, false, false, false, false, false };
			g_szSWEPFieldError[0] = 0;
			if( !XGLua_ProtectedCall( L, XGLua_SWEPFieldCore, &fc, XGLua_SWEPFieldError, classname ) )
			{
				ALERT( at_console, "[XGLua] %s: field read failed, weapon skipped: %s\n", classname, g_szSWEPFieldError[0] ? g_szSWEPFieldError : "unknown error" );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
			if( fc.abstract ) { luaL_unref( L, LUA_REGISTRYINDEX, tableRef ); lua_pop( L, 2 ); continue; }
			bool extraModelsOk = true;
			for( int m = 0; m < swep->numExtraModels; ++m )
				if( !XGLua_ModelExists( swep->extraModels[m] ) ) { extraModelsOk = false; break; }
			if( !extraModelsOk || !XGLua_ModelExists( swep->viewmodel ) || !XGLua_ModelExists( swep->worldmodel ) || !XGLua_ModelExists( swep->playermodel ) )
			{
				ALERT( at_console, "[XGLua] %s: missing or invalid SWEP model; weapon not registered\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
			if( !fc.soundsValid ||
				!XGLua_ValidateSoundResources( soundPaths, soundCount, g_XGLuaSounds, g_nXGLuaSounds, XGLua_SoundExists ) )
			{
				ALERT( at_console, "[XGLua] %s: invalid, missing, or excessive PrecacheSounds; weapon not registered\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
					int id = 16;
			while( id < WEAPON_SUIT && CBasePlayerItem::ItemInfoArray[id].iId ) ++id;
			if( id >= WEAPON_SUIT ) { ALERT( at_console, "[XGLua] no free weapon ID for %s\n", classname ); luaL_unref( L, LUA_REGISTRYINDEX, tableRef ); lua_pop( L, 2 ); continue; }
			swep->id = id;
			strncpy( swep->classname, classname, sizeof( swep->classname ) - 1 );
			// v1: the client HUD only knows vanilla weapon names — report the
			// 9mm pistol alias (design doc 9.5 P1; P2 removes this alias)
			bool explicitSecondaryMax = fc.explicitSecondaryMax;
			bool validSecondary = fc.validSecondary;
			if( !swep->ammo1[0] )
				strcpy( swep->ammo1, "none" );
			// Infinite-ammo weapons have no reserve to refill a finite clip.
			// Reject that unsupported combination instead of spawning a dead weapon.
			if( !XGLua_ValidAmmoClipConfig( swep->ammo1, swep->clipsize ) )
			{
				ALERT( at_console, "[XGLua] %s: invalid ClipSize/Ammo; use -1 for clipless, positive clip with reserve ammo\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
			if( !swep->ammo2[0] ) strcpy( swep->ammo2, "none" );
			// Native ammo lookup is case-insensitive, but MaxAmmoCarry uses exact
			// names and the first ItemInfo entry. Canonicalize and share its limit.
			for( int i = 1; i < MAX_WEAPONS; ++i )
			{
				const ItemInfo &existing = CBasePlayerItem::ItemInfoArray[i];
				if( !existing.iId ) continue;
				const char *names[2] = { existing.pszAmmo1, existing.pszAmmo2 };
				const int limits[2] = { existing.iMaxAmmo1, existing.iMaxAmmo2 };
				for( int j = 0; j < 2; ++j ) if( names[j] )
				{
					if( !stricmp( swep->ammo1, names[j] ) ) { if( fc.explicitPrimaryMax && swep->maxammo != limits[j] ) fc.validPrimary = false; strncpy( swep->ammo1, names[j], sizeof( swep->ammo1 ) - 1 ); swep->maxammo = limits[j]; }
					if( !stricmp( swep->ammo2, names[j] ) )
					{
						if( explicitSecondaryMax && swep->maxammo2 != limits[j] ) validSecondary = false;
						strncpy( swep->ammo2, names[j], sizeof( swep->ammo2 ) - 1 );
						swep->maxammo2 = limits[j];
					}
				}
			}
			if( !stricmp( swep->ammo1, swep->ammo2 ) && strcmp( swep->ammo1, "none" ) )
			{
				if( explicitSecondaryMax && swep->maxammo2 != swep->maxammo ) validSecondary = false;
				swep->maxammo2 = swep->maxammo;
			}
			if( !strcmp( swep->ammo2, "none" ) && swep->defaultammo2 ) validSecondary = false;
			if( !fc.validPrimary )
			{
				ALERT( at_console, "[XGLua] %s: invalid primary ammo configuration or conflicting ammo limit\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
			if( !validSecondary )
			{
				ALERT( at_console, "[XGLua] %s: invalid secondary ammo configuration or conflicting ammo limit\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}
			// GoldSrc wraps the ammo registry to slot 0 on overflow; refuse to
			// register new types when there is insufficient room.
			int newAmmoNames = 0;
			const char *requested[2] = { swep->ammo1, swep->ammo2 };
			for( int a = 0; a < 2; ++a ) if( strcmp( requested[a], "none" ) )
			{
				bool exists = ( a == 1 && !stricmp( requested[0], requested[1] ) );
				for( int j = 1; j < MAX_AMMO_SLOTS && !exists; ++j )
					if( CBasePlayerItem::AmmoInfoArray[j].pszName && !stricmp( CBasePlayerItem::AmmoInfoArray[j].pszName, requested[a] ) ) exists = true;
				if( !exists ) ++newAmmoNames;
			}
			if( giAmmoIndex + newAmmoNames >= MAX_AMMO_SLOTS )
			{
				ALERT( at_console, "[XGLua] no free ammo slots for %s\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}

			// The stock HUD stores only one weapon per slot/position pair.
			bool foundPosition = false;
			for( int offset = 0; offset < MAX_WEAPON_SLOTS * MAX_WEAPON_SLOTS; ++offset )
			{
				int cell = ( swep->slot * MAX_WEAPON_SLOTS + swep->slotpos + offset ) % ( MAX_WEAPON_SLOTS * MAX_WEAPON_SLOTS );
				int slot = cell / MAX_WEAPON_SLOTS, position = cell % MAX_WEAPON_SLOTS;
				bool occupied = false;
				for( int i = 1; i < MAX_WEAPONS; ++i )
				{
					const ItemInfo &existing = CBasePlayerItem::ItemInfoArray[i];
					if( existing.iId && existing.iSlot == slot && existing.iPosition == position ) { occupied = true; break; }
				}
				if( !occupied ) { swep->slot = slot; swep->slotpos = position; foundPosition = true; break; }
			}
			if( !foundPosition )
			{
				ALERT( at_console, "[XGLua] no free HUD slot for %s\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 2 );
				continue;
			}

			// Registry store comes first: it is the only raise-capable step left
			// (newtable/rawset allocate), so a failure here still leaves nothing
			// committed for this weapon. The engine writes below cannot raise.
			{
				XGLua_StoreSWEPTableCtx sc = { tableRef, swep->classname };
				g_szSWEPFieldError[0] = 0;
				if( !XGLua_ProtectedCall( L, XGLua_StoreSWEPTableCore, &sc, XGLua_SWEPFieldError, classname ) )
				{
					ALERT( at_console, "[XGLua] %s: registry store failed, weapon skipped: %s\n", classname, g_szSWEPFieldError[0] ? g_szSWEPFieldError : "unknown error" );
					luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
					lua_pop( L, 2 );
					continue;
				}
			}

			// All checks passed - commit the cache entry, then touch the
			// engine. Nothing below can raise a Lua error.
			swep = &g_SWEPs[g_nSWEPs];
			*swep = local;
			g_nSWEPs++;

			XGLuaCLSWEPMeta meta;
			std::memset( &meta, 0, sizeof( meta ) );
			meta.id = swep->id;
			std::strncpy( meta.classname, swep->classname, sizeof( meta.classname ) - 1 );
			meta.slot = swep->slot;
			meta.slotpos = swep->slotpos;
			meta.primaryAutomatic = swep->primaryAutomatic;
			meta.secondaryAutomatic = swep->secondaryAutomatic;
			meta.clipsize = swep->clipsize;
			g_SVSWEPMeta.Upsert( meta );

			// fill the ItemInfoArray slot — mirrors UTIL_PrecacheOtherWeapon
			ItemInfo &II = swep->info;
			memset( &II, 0, sizeof( II ) );
			II.pszAmmo1 = strcmp( swep->ammo1, "none" ) ? swep->ammo1 : NULL;
			II.iMaxAmmo1 = swep->maxammo;
			II.pszAmmo2 = strcmp( swep->ammo2, "none" ) ? swep->ammo2 : NULL;
			II.iMaxAmmo2 = II.pszAmmo2 ? swep->maxammo2 : -1;
			II.iMaxClip = swep->clipsize;
			II.iSlot = swep->slot;
			II.iPosition = swep->slotpos;
			II.pszName = swep->classname;
			II.iId = swep->id;
			II.iFlags = 0;
			II.iWeight = swep->weight ? swep->weight : 5;

			// All paths and files have been checked before any sound is registered.
			// The engine does not expose its global remaining sound capacity.
			for( int i = 0; i < soundCount; ++i )
			{
				bool known = false;
				for( int j = 0; j < g_nXGLuaSounds && !known; ++j )
					known = XGLua_SoundNamesEqual( soundPaths[i], g_XGLuaSounds[j] );
				if( !known )
				{
					PRECACHE_SOUND( soundPaths[i] );
					strcpy( g_XGLuaSounds[g_nXGLuaSounds++], soundPaths[i] );
				}
			}

			CBasePlayerItem::ItemInfoArray[II.iId] = II;
			if( II.pszAmmo1 && *II.pszAmmo1 )
				AddAmmoNameToAmmoRegistry( II.pszAmmo1 );
			if( II.pszAmmo2 && *II.pszAmmo2 )
				AddAmmoNameToAmmoRegistry( II.pszAmmo2 );

			// precache SWEP models now — we are inside the server spawn
			// phase, the only window where Precache is legal
			if( swep->viewmodel[0] )
				PRECACHE_MODEL( (char *)swep->viewmodel );
			if( swep->worldmodel[0] )
				PRECACHE_MODEL( (char *)swep->worldmodel );
			if( swep->playermodel[0] )
				PRECACHE_MODEL( (char *)swep->playermodel );

			for( int m = 0; m < swep->numExtraModels; ++m )
				PRECACHE_MODEL( swep->extraModels[m] );


			ALERT( at_console, "[XGLua] registered SWEP: %s\n", swep->classname );
			// Release after the engine cache is fully committed so even an
			// allocation failure here cannot leave a registry-only weapon.
			luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
		}
		lua_pop( L, 2 );
	}
	lua_pop( L, 1 );	// sorted names
	lua_pop( L, 1 );	// GetStored table
	lua_pop( L, 1 );	// weapons table
	return 0;
}

void XGLua_BuildWeaponCache( lua_State *L )
{
	g_SVSWEPMeta.Clear();
	g_nSWEPs = 0;
	g_nXGLuaSounds = 0;
	// The whole build runs inside one protected frame as a safety net;
	// per-weapon field reads are isolated in their own protected calls, so a
	// broken SWEP is skipped with a report while the remaining classes keep
	// registering. Entries committed so far always stay consistent.
	// is restored either way.
	XGLua_ProtectedCall( L, XGLua_BuildWeaponCacheCore, NULL, XGLua_ReportError, "weapon cache build" );
}

// Snapshot names, not just numeric IDs: the script set may change between
// save and restore and the engine stores inventory as numeric arrays.
void XGLua_PreparePlayerSave( CBasePlayer *player )
{
    player->m_xglSaveVersion = 1;
    for( int i=0; i<MAX_AMMO_SLOTS; ++i )
    {
        const char *name=CBasePlayerItem::AmmoInfoArray[i].pszName;
        player->m_xglAmmoNames[i]=name && *name ? ALLOC_STRING(name) : 0;
    }
    for( int i=0; i<MAX_WEAPONS; ++i )
    {
        const ItemInfo &info=CBasePlayerItem::ItemInfoArray[i];
        player->m_xglWeaponNames[i]=info.iId && info.pszName && *info.pszName ? ALLOC_STRING(info.pszName) : 0;
    }
}

static void XGLua_ReconcileRestoredPlayer( CBasePlayer *player, edict_t *list, int count, int clientMax )
{
    if( !player || !player->m_xglRestorePending ) return;
    player->m_xglRestorePending = false;
    if( player->m_xglSaveVersion == 1 )
    {
        const char *oldAmmo[MAX_AMMO_SLOTS], *newAmmo[MAX_AMMO_SLOTS];
        int scratch[MAX_AMMO_SLOTS];
        for( int i=0; i<MAX_AMMO_SLOTS; ++i ) {
            oldAmmo[i]=player->m_xglAmmoNames[i] ? STRING(player->m_xglAmmoNames[i]) : NULL;
            newAmmo[i]=CBasePlayerItem::AmmoInfoArray[i].pszName;
        }
        XGLua_RemapSavedAmmo( player->m_rgAmmo, MAX_AMMO_SLOTS, oldAmmo, newAmmo, scratch );
    }
    else
        ALERT( at_console, "[XGLua] old player save lacks ammo name map; preserving legacy numeric ammo slots\n" );

    // Never follow the serialized player chain here: Xash may have culled an
    // earlier item edict (EntityInSolid + SV_FreeOldEntities) during restore.
    CBasePlayerItem *items[MAX_EDICTS] = {};
    bool removedActive=false;
    int itemCount=XGLua_CollectOwnedItems( list, count, clientMax+1, items, MAX_EDICTS,
        []( edict_t *edict ) { return !edict->free && edict->pvPrivateData; },
        []( edict_t *edict ) {
            if( XGLua_IsTrackedWeapon( edict ) ) return true;
            const char *classname=STRING( edict->v.classname );
            if( !classname ) return false;
            for( int id=1; id<MAX_WEAPONS; ++id )
                if( CBasePlayerItem::ItemInfoArray[id].iId && CBasePlayerItem::ItemInfoArray[id].pszName && FStrEq( classname, CBasePlayerItem::ItemInfoArray[id].pszName ) ) return true;
            return false;
        },
        []( edict_t *edict ) { return (CBasePlayerItem *)edict->pvPrivateData; },
        [player]( edict_t *edict, CBasePlayerItem *item ) { return edict->v.owner==player->edict() && item->m_pPlayer==player; } );
    CBasePlayerItem *active=player->m_pActiveItem, *last=player->m_pLastItem;
    removedActive=active!=NULL;
    itemCount=XGLua_FilterMissingRestoreItems( items, itemCount, active, last, removedActive,
        []( CBasePlayerItem *item ) { return XGLua_IsTrackedWeapon( item->edict() ) && ((CLuaWeapon *)item)->m_xglRestoreMissing; },
        [player]( CBasePlayerItem *item ) { item->m_pPlayer=NULL; UTIL_Remove( item ); } );
    player->m_pActiveItem=active;
    player->m_pLastItem=last;
    for( int itemIndex=0; itemIndex<itemCount; ++itemIndex )
    {
        CBasePlayerItem *item=items[itemIndex];
        CBasePlayerWeapon *weapon=(CBasePlayerWeapon *)item;
        int primary=weapon->pszAmmo1() ? player->GetAmmoIndex(weapon->pszAmmo1()) : -1;
        int secondary=weapon->pszAmmo2() ? player->GetAmmoIndex(weapon->pszAmmo2()) : -1;
        weapon->m_iPrimaryAmmoType=primary;
        weapon->m_iSecondaryAmmoType=secondary;
        weapon->m_iClientClip=-1;
        weapon->m_iClientWeaponState=-1;
        items[itemIndex]=item;
    }
    XGLua_RebuildItemChains( items, itemCount, player->m_rgpPlayerItems, MAX_ITEM_TYPES,
        active, last, []( CBasePlayerItem *item ) { int slot=item->iItemSlot(); return slot<0 || slot>=MAX_ITEM_TYPES ? -1 : slot; } );
    player->m_pActiveItem=active;
    player->m_pLastItem=last;
    unsigned bits=player->pev->weapons & ( 1u << WEAPON_SUIT );
    bits=XGLua_BuildWeaponBits( player->m_rgpPlayerItems, MAX_ITEM_TYPES, bits,
        []( CBasePlayerItem *item ) { return item->m_iId; } );
    player->pev->weapons=(int)bits;
    if( removedActive && !active ) {
        player->pev->viewmodel=0;
        player->pev->weaponmodel=0;
    }
    player->m_fKnownItem=FALSE;
    player->m_fWeapon=FALSE;
    player->m_pClientActiveItem=NULL;
    for( int i=0; i<MAX_AMMO_SLOTS; ++i ) player->m_rgAmmoLast[i]=-1;
    player->TabulateAmmo();
}
void XGLua_ReconcileRestoredPlayers( edict_t *list, int count, int clients )
{
    if( !list || count<=1 ) return;
    for( int i=1; i<=clients && i<count; ++i )
        if( !list[i].free && list[i].pvPrivateData )
        {
            CBaseEntity *entity=CBaseEntity::Instance( &list[i] );
        if( entity && entity->IsPlayer() && XGLua_IsRestoredPlayer( (CBasePlayer *)entity ) )
            XGLua_ReconcileRestoredPlayer( (CBasePlayer *)entity, list, count, clients );
        }

    // Weapon boxes own linked lists of items as well. Unlink missing SWEPs
    // before scheduling them for removal, leaving all native items intact.
    for( int i=clients+1; i<count; ++i )
    {
        edict_t *edict=&list[i];
        if( edict->free || !edict->pvPrivateData || !FClassnameIs( &edict->v, "weaponbox" ) ) continue;
        CWeaponBox *box=(CWeaponBox *)CBaseEntity::Instance( edict );
        CBasePlayerItem *boxItems[MAX_EDICTS] = {};
        int itemCount=XGLua_CollectOwnedItems( list, count, clients+1, boxItems, MAX_EDICTS,
            []( edict_t *candidate ) { return !candidate->free && candidate->pvPrivateData; },
            []( edict_t *candidate ) {
                if( XGLua_IsTrackedWeapon( candidate ) ) return true;
                const char *classname=STRING( candidate->v.classname );
                if( !classname ) return false;
                for( int id=1; id<MAX_WEAPONS; ++id )
                    if( CBasePlayerItem::ItemInfoArray[id].iId && CBasePlayerItem::ItemInfoArray[id].pszName && FStrEq( classname, CBasePlayerItem::ItemInfoArray[id].pszName ) ) return true;
                return false;
            },
            []( edict_t *candidate ) { return (CBasePlayerItem *)candidate->pvPrivateData; },
            [edict]( edict_t *candidate, CBasePlayerItem *item ) { return candidate->v.owner==edict && item->m_pPlayer==NULL; } );
        int validCount=0;
        for( int j=0; j<itemCount; ++j )
        {
            CBasePlayerItem *item=boxItems[j];
            if( XGLua_IsTrackedWeapon( item->edict() ) && ((CLuaWeapon *)item)->m_xglRestoreMissing )
            {
                item->m_pNext=NULL;
                UTIL_Remove( item );
                continue;
            }
            CBasePlayerWeapon *weapon=(CBasePlayerWeapon *)item;
            weapon->m_iPrimaryAmmoType=weapon->pszAmmo1() ? CBasePlayer::GetAmmoIndex(weapon->pszAmmo1()) : -1;
            weapon->m_iSecondaryAmmoType=weapon->pszAmmo2() ? CBasePlayer::GetAmmoIndex(weapon->pszAmmo2()) : -1;
            boxItems[validCount++]=item;
        }
        CBasePlayerItem *unusedActive=NULL, *unusedLast=NULL;
        XGLua_RebuildItemChains( boxItems, validCount, box->m_rgpPlayerItems, MAX_ITEM_TYPES,
            unusedActive, unusedLast, []( CBasePlayerItem *item ) { int slot=item->iItemSlot(); return slot<0 || slot>=MAX_ITEM_TYPES ? -1 : slot; } );
    }

    // Includes unowned ground weapons; player and box chains were unlinked
    // first. UTIL_Remove is deferred, so skip entities already marked for it.
    for( size_t i=0; i<g_TrackedWeapons.size(); ++i )
    {
        edict_t *edict=g_TrackedWeapons[i].edict;
        if( !XGLua_IsTrackedWeapon( edict ) || (edict->v.flags & FL_KILLME) ) continue;
        CLuaWeapon *weapon=(CLuaWeapon *)edict->pvPrivateData;
        if( weapon->m_xglRestoreMissing ) UTIL_Remove( weapon );
    }
}
