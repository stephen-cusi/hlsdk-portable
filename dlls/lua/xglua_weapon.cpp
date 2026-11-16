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

#include <cstring>
#include <cstdio>

#include "xglua.h"
#include "xglua_swep_bridge.h"
#include "weapons.h"
#include "player.h"
extern "C" {
#include "lauxlib.h"
}

extern void AddAmmoNameToAmmoRegistry( const char *szAmmoname );	// weapons.cpp

//===========================================================================
// SWEP C++ cache — read once from the Lua SWEP table at startup
//===========================================================================

struct XGLuaSWEPInfo
{
	char classname[64];
	char viewmodel[128];
	char worldmodel[128];
	char ammo1[32];
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

static XGLuaSWEPInfo *XGLua_FindSWEP( const char *classname )
{
	for( int i = 0; i < g_nSWEPs; i++ )
	{
		if( !strcmp( g_SWEPs[i].classname, classname ) )
			return &g_SWEPs[i];
	}
	return NULL;
}

//===========================================================================
// CLuaWeapon
//===========================================================================

class CLuaWeapon : public CBasePlayerWeapon
{
public:
	char m_szLuaClass[64];

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

	void InitFromCache( const char *classname );
};

static void CallWeaponMethod( CLuaWeapon *w, const char *method )
{
	lua_State *L = g_xglua_L;
	if( !L || !w )
		return;

	if( !XGLua_PushSWEPMethod( L, w->m_szLuaClass, method ) )
		return;
	XGLua_PushEntity( L, w->edict() );

	if( lua_pcall( L, 1, 0, 0 ) != 0 )
	{
		ALERT( at_console, "[XGLua] SWEP:%s error:\n%s\n", method, lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
	}
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
	m_iClip = ( swep && swep->clipsize < 0 ) ? WEAPON_NOCLIP : 0;
	m_fInReload = 0;
	m_fFireOnEmpty = FALSE;
	m_iPlayEmptySound = 1;
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
		// v_9mmhandgun draw sequence = 7 (see weapon_9mmhandgun.cpp)
		ok = DefaultDeploy( swep->viewmodel, swep->worldmodel, 7, "onehanded" );
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
	}
	return CBasePlayerWeapon::CanDeploy();
}

void CLuaWeapon::ItemPostFrame( void )
{
	if( !m_pPlayer ) return;
	// Complete a pending DefaultReload using the engine's server clock.
	if( m_fInReload && m_pPlayer->m_flNextAttack <= UTIL_WeaponTimeBase() )
	{
		int n = Q_min( iMaxClip() - m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] );
		m_iClip += n;
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= n;
		m_pPlayer->TabulateAmmo();
		m_fInReload = FALSE;
	}
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	if( !swep ) return;
	int buttons = m_pPlayer->pev->button;
	int pressed = m_pPlayer->m_afButtonPressed;
	if( ( buttons & IN_ATTACK2 ) && ( swep->secondaryAutomatic || ( pressed & IN_ATTACK2 ) ) && gpGlobals->time >= m_flNextSecondaryAttack )
		SecondaryAttack();
	else if( ( buttons & IN_ATTACK ) && ( swep->primaryAutomatic || ( pressed & IN_ATTACK ) ) && gpGlobals->time >= m_flNextPrimaryAttack )
		PrimaryAttack();
	else if( ( buttons & IN_RELOAD ) && !m_fInReload )
		Reload();
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

static int Wep_DefaultReload( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int clip = (int)luaL_checknumber( L, 2 );
	int anim = (int)luaL_checknumber( L, 3 );
	float delay = (float)luaL_checknumber( L, 4 );
	if( !w->m_pPlayer || w->PrimaryAmmoIndex() < 0 || w->m_fInReload ||
		clip <= w->m_iClip || clip > w->iMaxClip() || delay < 0 )
	{ lua_pushboolean( L, false ); return 1; }
	lua_pushboolean( L, w->DefaultReload( clip, anim, delay ) );
	return 1;
}

static int Wep_SendWeaponAnim( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	int seq = (int)luaL_checknumber( L, 2 );
	w->SendWeaponAnim( seq, 0, 0 );	// skiplocal 0: no client prediction for Lua weapons
	return 0;
}

static int Wep_PlayEmptySound( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushboolean( L, w->PlayEmptySound() );
	return 1;
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
			{ "DefaultReload", Wep_DefaultReload },
			{ "SendWeaponAnim", Wep_SendWeaponAnim },
			{ "PlayEmptySound", Wep_PlayEmptySound },
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

void XGLua_BuildWeaponCache( lua_State *L )
{
	g_nSWEPs = 0;
	// iterate weapons.GetStored() (Lua side, filled by weapons.Register)
	lua_getglobal( L, "weapons" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_getfield( L, -1, "GetStored" );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return;
	}
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		ALERT( at_console, "[XGLua] weapons.GetStored error: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return;
	}

	lua_pushnil( L );
	while( lua_next( L, -2 ) != 0 )
	{
		// [-2] key (classname), [-1] SWEP table
		if( lua_isstring( L, -2 ) && lua_istable( L, -1 ) )
		{
			const char *classname = lua_tostring( L, -2 );
			if( g_nSWEPs >= XGLUA_MAX_SWEP )
			{
				ALERT( at_console, "[XGLua] too many SWEPs, ignoring %s\n", classname );
				lua_pop( L, 1 );
				continue;
			}

			lua_getfield( L, -1, "Abstract" );
			bool abstract = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
			if( abstract ) { lua_pop( L, 1 ); continue; }
					int id = 16;
			while( id < WEAPON_SUIT && CBasePlayerItem::ItemInfoArray[id].iId ) ++id;
			if( id >= WEAPON_SUIT ) { ALERT( at_console, "[XGLua] no free weapon ID for %s\n", classname ); lua_pop( L, 1 ); continue; }
			XGLuaSWEPInfo *swep = &g_SWEPs[g_nSWEPs++];
			memset( swep, 0, sizeof( XGLuaSWEPInfo ) );
			swep->id = id;
			strncpy( swep->classname, classname, sizeof( swep->classname ) - 1 );
			// v1: the client HUD only knows vanilla weapon names — report the
			// 9mm pistol alias (design doc 9.5 P1; P2 removes this alias)
			strcpy( swep->hudname, "weapon_9mmhandgun" );

			lua_getfield( L, -1, "ViewModel" );
			if( lua_isstring( L, -1 ) )
				strncpy( swep->viewmodel, lua_tostring( L, -1 ), sizeof( swep->viewmodel ) - 1 );
			lua_pop( L, 1 );

			lua_getfield( L, -1, "WorldModel" );
			if( lua_isstring( L, -1 ) )
				strncpy( swep->worldmodel, lua_tostring( L, -1 ), sizeof( swep->worldmodel ) - 1 );
			lua_pop( L, 1 );

			lua_getfield( L, -1, "Slot" );
			swep->slot = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 2;
			swep->slot = Q_max( 0, Q_min( swep->slot, MAX_WEAPON_SLOTS - 1 ) );
			lua_pop( L, 1 );

			lua_getfield( L, -1, "SlotPos" );
			swep->slotpos = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 1;
			swep->slotpos = Q_max( 0, Q_min( swep->slotpos, MAX_WEAPON_SLOTS - 1 ) );
			lua_pop( L, 1 );

			// SWEP.Primary sub-table
			lua_getfield( L, -1, "Primary" );
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
			}
			lua_pop( L, 1 );

			lua_getfield( L, -1, "Secondary" );
			if( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, "Automatic" );
				swep->secondaryAutomatic = lua_toboolean( L, -1 ) != 0;
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );

			if( !swep->ammo1[0] )
				strcpy( swep->ammo1, "none" );
			// Infinite-ammo weapons have no reserve to refill a finite clip.
			// Reject that unsupported combination instead of spawning a dead weapon.
			if( !XGLua_ValidAmmoClipConfig( swep->ammo1, swep->clipsize ) )
			{
				ALERT( at_console, "[XGLua] %s: invalid ClipSize/Ammo; use -1 for clipless, positive clip with reserve ammo\n", classname );
				--g_nSWEPs;
				lua_pop( L, 1 );
				continue;
			}
			swep->maxammo = 68;	// default reserve limit

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
				--g_nSWEPs;
				lua_pop( L, 1 );
				continue;
			}

			// fill the ItemInfoArray slot — mirrors UTIL_PrecacheOtherWeapon
			ItemInfo &II = swep->info;
			memset( &II, 0, sizeof( II ) );
			II.pszAmmo1 = strcmp( swep->ammo1, "none" ) ? swep->ammo1 : NULL;
			II.iMaxAmmo1 = swep->maxammo;
			II.pszAmmo2 = NULL;
			II.iMaxAmmo2 = -1;
			II.iMaxClip = swep->clipsize;
			II.iSlot = swep->slot;
			II.iPosition = swep->slotpos;
			II.pszName = swep->classname;
			II.iId = swep->id;
			II.iFlags = 0;
			II.iWeight = swep->weight ? swep->weight : 5;

			CBasePlayerItem::ItemInfoArray[II.iId] = II;
			if( II.pszAmmo1 && *II.pszAmmo1 )
				AddAmmoNameToAmmoRegistry( II.pszAmmo1 );

			// precache SWEP models now — we are inside the server spawn
			// phase, the only window where Precache is legal
			if( swep->viewmodel[0] )
				PRECACHE_MODEL( (char *)swep->viewmodel );
			if( swep->worldmodel[0] )
				PRECACHE_MODEL( (char *)swep->worldmodel );

			// Keep the actual table, not the lua_next classname key.
			XGLua_StoreSWEPTable( L, -1, swep->classname );

			ALERT( at_console, "[XGLua] registered SWEP: %s\n", swep->classname );
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );	// GetStored table
	lua_pop( L, 1 );	// weapons table
}
