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

	XGLua_GetSWEPRegistry( L );
	lua_pushstring( L, w->m_szLuaClass );
	lua_rawget( L, -2 );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 2 );
		return;
	}
	lua_getfield( L, -1, method );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 3 );
		return;
	}
	lua_remove( L, -2 );	// drop SWEP table
	lua_remove( L, -2 );	// drop registry
	XGLua_PushEntity( L, w->edict() );

	if( lua_pcall( L, 1, 0, 0 ) != 0 )
		ALERT( at_console, "[XGLua] SWEP:%s error:\n%s\n", method, lua_tostring( L, -1 ) );
	lua_pop( L, 1 );
}

void CLuaWeapon::InitFromCache( const char *classname )
{
	strncpy( m_szLuaClass, classname, sizeof( m_szLuaClass ) - 1 );
	m_szLuaClass[sizeof( m_szLuaClass ) - 1] = '\0';

	XGLuaSWEPInfo *swep = XGLua_FindSWEP( classname );
	m_iId = WEAPON_GLOCK;	// v1: reuse the 9mm pistol id so the client HUD knows us
	m_iDefaultAmmo = swep ? swep->defaultclip : 0;
	m_iClip = -1;
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
	m_iId = WEAPON_GLOCK;

	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
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
	CBasePlayerItem::Holster( skiplocal );
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
	// mirror the base class bookkeeping, then route input to Lua
	if( !m_pPlayer )
		return;

	m_flNextPrimaryAttack = Q_max( m_flNextPrimaryAttack - gpGlobals->frametime, -1.0f );
	m_flNextSecondaryAttack = Q_max( m_flNextSecondaryAttack - gpGlobals->frametime, -1.0f );
	m_flTimeWeaponIdle = Q_max( m_flTimeWeaponIdle - gpGlobals->frametime, -1.0f );

	int buttons = m_pPlayer->pev->button;

	if( ( buttons & IN_ATTACK ) && gpGlobals->time >= m_flNextPrimaryAttack )
	{
		PrimaryAttack();
	}
	else if( ( buttons & IN_ATTACK2 ) && gpGlobals->time >= m_flNextSecondaryAttack )
	{
		SecondaryAttack();
	}

	if( ( buttons & IN_RELOAD ) && gpGlobals->time >= m_flNextPrimaryAttack )
	{
		Reload();
	}
}

int CLuaWeapon::GetItemInfo( ItemInfo *p )
{
	XGLuaSWEPInfo *swep = XGLua_FindSWEP( m_szLuaClass );
	if( !swep )
		return 0;
	*p = swep->info;
	p->pszName = m_szLuaClass;
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
	pPlayer->SwitchWeapon( pItem );

	return pItem;
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
	w->m_flNextPrimaryAttack = (float)( t - gpGlobals->time );
	return 0;
}

static int Wep_GetNextPrimaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushnumber( L, gpGlobals->time + w->m_flNextPrimaryAttack );
	return 1;
}

static int Wep_SetNextSecondaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	double t = luaL_checknumber( L, 2 );
	w->m_flNextSecondaryAttack = (float)( t - gpGlobals->time );
	return 0;
}

static int Wep_GetNextSecondaryFire( lua_State *L )
{
	CLuaWeapon *w = WeaponFromLua( L, 1 );
	lua_pushnumber( L, gpGlobals->time + w->m_flNextSecondaryAttack );
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
	int iType = w->PrimaryAmmoIndex();
	if( iType >= 0 && w->m_pPlayer && w->m_pPlayer->m_rgAmmo[iType] > 0 )
	{
		int take = n < w->m_pPlayer->m_rgAmmo[iType] ? n : w->m_pPlayer->m_rgAmmo[iType];
		w->m_pPlayer->m_rgAmmo[iType] -= take;
	}
	return 0;
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

			XGLuaSWEPInfo *swep = &g_SWEPs[g_nSWEPs++];
			memset( swep, 0, sizeof( XGLuaSWEPInfo ) );
			strncpy( swep->classname, classname, sizeof( swep->classname ) - 1 );

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
			lua_pop( L, 1 );

			lua_getfield( L, -1, "SlotPos" );
			swep->slotpos = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 1;
			lua_pop( L, 1 );

			// SWEP.Primary sub-table
			lua_getfield( L, -1, "Primary" );
			if( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, "ClipSize" );
				swep->clipsize = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : -1;
				lua_pop( L, 1 );

				lua_getfield( L, -1, "DefaultClip" );
				swep->defaultclip = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 0;
				lua_pop( L, 1 );

				lua_getfield( L, -1, "Ammo" );
				if( lua_isstring( L, -1 ) )
					strncpy( swep->ammo1, lua_tostring( L, -1 ), sizeof( swep->ammo1 ) - 1 );
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );

			if( !swep->ammo1[0] )
				strcpy( swep->ammo1, "9mm" );
			swep->maxammo = 68;	// HL1 9mm carry max

			// fill the ItemInfoArray slot — mirrors UTIL_PrecacheOtherWeapon
			ItemInfo &II = swep->info;
			memset( &II, 0, sizeof( II ) );
			II.pszAmmo1 = swep->ammo1;
			II.iMaxAmmo1 = swep->maxammo;
			II.pszAmmo2 = NULL;
			II.iMaxAmmo2 = -1;
			II.iMaxClip = swep->clipsize;
			II.iSlot = swep->slot;
			II.iPosition = swep->slotpos;
			II.pszName = swep->classname;
			II.iId = WEAPON_GLOCK;	// v1: shared with the 9mm pistol id
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

			// strong-ref the SWEP table: registry[XGLUA_SWEP_REGISTRY][classname]
			XGLua_GetSWEPRegistry( L );
			lua_pushstring( L, swep->classname );
			lua_pushvalue( L, -4 );	// SWEP table
			lua_settable( L, -3 );
			lua_pop( L, 1 );

			ALERT( at_console, "[XGLua] registered SWEP: %s\n", swep->classname );
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );	// GetStored table
	lua_pop( L, 1 );	// weapons table
}
