/***
*
*	XGLua — Entity handles + ents / player / util libraries
*
*	Entity handles are { edict_t*, serial } pairs: a deleted entity's
*	handle becomes invalid instead of dangling (GMod "NULL entity" model).
*
****/

#include <cmath>
#include <cstring>

#include "xglua.h"
#include "weapons.h"
#include "player.h"
#include "enginecallback.h"
extern "C" {
#include "lauxlib.h"
}

#define XGLUA_ENTITY_META "XGLua_Entity"

//===========================================================================
// entity handle
//===========================================================================

struct XGLuaEntRef
{
	edict_t *pent;
	int serial;
};

static edict_t *XGLua_ValidateEnt( XGLuaEntRef *ref )
{
	if( !ref || !ref->pent )
		return NULL;
	if( ref->pent->free || ref->pent->serialnumber != ref->serial )
		return NULL;
	return ref->pent;
}

void XGLua_PushEntity( lua_State *L, edict_t *pent )
{
	if( FNullEnt( pent ) )
	{
		lua_pushnil( L );
		return;
	}
	XGLuaEntRef *ref = (XGLuaEntRef *)lua_newuserdata( L, sizeof( XGLuaEntRef ) );
	ref->pent = pent;
	ref->serial = pent->serialnumber;
	luaL_getmetatable( L, XGLUA_ENTITY_META );
	lua_setmetatable( L, -2 );
}

edict_t *XGLua_CheckEntity( lua_State *L, int idx )
{
	XGLuaEntRef *ref = (XGLuaEntRef *)luaL_checkudata( L, idx, XGLUA_ENTITY_META );
	edict_t *pent = XGLua_ValidateEnt( ref );
	if( !pent )
		luaL_error( L, "Tried to use a NULL entity!" );
	return pent;
}

static BOOL XGLua_OptEntity( lua_State *L, int idx, edict_t **ppent )
{
	if( lua_isnil( L, idx ) )
	{
		*ppent = NULL;
		return FALSE;
	}
	*ppent = XGLua_CheckEntity( L, idx );
	return TRUE;
}

//===========================================================================
// helpers
//===========================================================================

static CBaseEntity *Ent( lua_State *L, int idx )
{
	return CBaseEntity::Instance( XGLua_CheckEntity( L, idx ) );
}

static int XGLua_IsValidEntity( lua_State *L, int idx )
{
	// IsValid: accepts nil, entity handles (validates serial), NULL entity
	if( lua_isnil( L, idx ) )
		return 0;
	if( !lua_isuserdata( L, idx ) )
		return lua_toboolean( L, idx );
	XGLuaEntRef *ref = (XGLuaEntRef *)luaL_checkudata( L, idx, XGLUA_ENTITY_META );
	return XGLua_ValidateEnt( ref ) != NULL;
}

//===========================================================================
// Entity methods
//===========================================================================

static int Ent_IsValid( lua_State *L )
{
	lua_pushboolean( L, XGLua_IsValidEntity( L, 1 ) );
	return 1;
}

static int Ent_EntIndex( lua_State *L )
{
	lua_pushnumber( L, ENTINDEX( XGLua_CheckEntity( L, 1 ) ) );
	return 1;
}

static int Ent_GetClass( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	lua_pushstring( L, STRING( VARS( pent )->classname ) );
	return 1;
}

static int Ent_GetPos( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, VARS( pent )->origin );
	return 1;
}

static int Ent_SetPos( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern Vector XGLua_CheckVector( lua_State *L, int idx );
	VARS( pent )->origin = XGLua_CheckVector( L, 2 );
	return 0;
}

static int Ent_GetAngles( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, VARS( pent )->angles );
	return 1;
}

static int Ent_SetAngles( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern Vector XGLua_CheckVector( lua_State *L, int idx );
	VARS( pent )->angles = XGLua_CheckVector( L, 2 );
	return 0;
}

static int Ent_GetVelocity( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, VARS( pent )->velocity );
	return 1;
}

static int Ent_SetVelocity( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern Vector XGLua_CheckVector( lua_State *L, int idx );
	VARS( pent )->velocity = XGLua_CheckVector( L, 2 );
	return 0;
}

static int Ent_SetSolid( lua_State *L )
{
	VARS( XGLua_CheckEntity( L, 1 ) )->solid = (int)luaL_checknumber( L, 2 );
	return 0;
}

static int Ent_SetMoveType( lua_State *L )
{
	VARS( XGLua_CheckEntity( L, 1 ) )->movetype = (int)luaL_checknumber( L, 2 );
	return 0;
}

static int Ent_SetCollisionBounds( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	extern Vector XGLua_CheckVector( lua_State *L, int idx );
	Vector mins = XGLua_CheckVector( L, 2 );
	Vector maxs = XGLua_CheckVector( L, 3 );
	VARS( pent )->mins = mins;
	VARS( pent )->maxs = maxs;
	return 0;
}

static int Ent_GetModel( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const char *s = STRING( VARS( pent )->model );
	if( !s || !s[0] )
	{
		lua_pushnil( L );
		return 1;
	}
	lua_pushstring( L, s );
	return 1;
}

static int Ent_SetModel( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	SET_MODEL( pent, luaL_checkstring( L, 2 ) );
	return 0;
}

static int Ent_GetHealth( lua_State *L )
{
	lua_pushnumber( L, VARS( XGLua_CheckEntity( L, 1 ) )->health );
	return 1;
}

static int Ent_SetHealth( lua_State *L )
{
	VARS( XGLua_CheckEntity( L, 1 ) )->health = (float)luaL_checknumber( L, 2 );
	return 0;
}

static int Ent_GetMaxHealth( lua_State *L )
{
	CBaseEntity *pEnt = Ent( L, 1 );
	lua_pushnumber( L, pEnt->pev->max_health );
	return 1;
}

static int Ent_SetMaxHealth( lua_State *L )
{
	VARS( XGLua_CheckEntity( L, 1 ) )->max_health = (float)luaL_checknumber( L, 2 );
	return 0;
}

static int Ent_IsPlayer( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	CBaseEntity *pEnt = CBaseEntity::Instance( pent );
	lua_pushboolean( L, pEnt && pEnt->IsPlayer() );
	return 1;
}

static int Ent_GetOwner( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	edict_t *pOwner = VARS( pent )->owner;
	if( FNullEnt( pOwner ) )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, pOwner );
	return 1;
}

static int Ent_SetOwner( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	edict_t *pOwner = NULL;
	XGLua_OptEntity( L, 2, &pOwner );
	VARS( pent )->owner = pOwner;
	return 0;
}

static int Ent_GetGroundEntity( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	edict_t *pGround = VARS( pent )->groundentity;
	if( FNullEnt( pGround ) )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, pGround );
	return 1;
}

static int Ent_EmitSound( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const char *name = luaL_checkstring( L, 2 );
	float vol = (float)luaL_optnumber( L, 3, 1.0 );
	EMIT_SOUND( pent, CHAN_ITEM, name, vol, ATTN_NORM );
	return 0;
}

static int Ent_StopSound( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const char *name = luaL_checkstring( L, 2 );
	EMIT_SOUND( pent, CHAN_ITEM, name, 0, ATTN_NORM );	// vol 0 stops
	return 0;
}

static int Ent_Spawn( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	DispatchSpawn( pent );
	return 0;
}

static int Ent_Remove( lua_State *L )
{
	CBaseEntity *pEnt = Ent( L, 1 );
	if( pEnt )
		pEnt->SUB_Remove();	// deferred remove, engine-safe
	return 0;
}

static int Ent_SetName( lua_State *L )
{
	VARS( XGLua_CheckEntity( L, 1 ) )->targetname = MAKE_STRING( luaL_checkstring( L, 2 ) );
	return 0;
}

static int Ent_GetName( lua_State *L )
{
	string_t name = VARS( XGLua_CheckEntity( L, 1 ) )->targetname;
	if( name == iStringNull )
	{
		lua_pushnil( L );
		return 1;
	}
	lua_pushstring( L, STRING( name ) );
	return 1;
}

static int Ent_Index( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const char *cls = STRING( VARS( pent )->classname );
	if( XGLua_IsLuaWeapon( cls ) )
	{
		XGLua_GetSWEPRegistry( L );
		lua_getfield( L, -1, cls );
		if( lua_istable( L, -1 ) )
		{
			lua_pushvalue( L, 2 );
			lua_gettable( L, -2 );
			if( !lua_isnil( L, -1 ) ) return 1;
			lua_pop( L, 1 );
		}
		lua_pop( L, 2 );
	}
	luaL_getmetatable( L, XGLUA_ENTITY_META );
	lua_pushvalue( L, 2 );
	lua_rawget( L, -2 );
	return 1;
}

//===========================================================================
// Player methods
//===========================================================================

static CBasePlayer *Ply( lua_State *L, int idx )
{
	CBaseEntity *pEnt = Ent( L, idx );
	if( !pEnt || !pEnt->IsPlayer() )
		luaL_error( L, "Tried to use a non-player entity as a Player!" );
	return (CBasePlayer *)pEnt;
}

static int Ply_Nick( lua_State *L )
{
	lua_pushstring( L, STRING( VARS( XGLua_CheckEntity( L, 1 ) )->netname ) );
	return 1;
}

static int Ply_Alive( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	lua_pushboolean( L, p->pev->health > 0 && p->pev->deadflag == DEAD_NO );
	return 1;
}

static int Ply_Kill( lua_State *L )
{
	Ply( L, 1 )->Killed( Ply( L, 1 )->pev, GIB_NEVER );
	return 0;
}

static int Ply_GetActiveWeapon( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	if( !p->m_pActiveItem )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, p->m_pActiveItem->edict() );
	return 1;
}

static int Ply_GetAimVector( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	UTIL_MakeVectors( p->pev->v_angle );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, gpGlobals->v_forward );
	return 1;
}

static int Ply_EyePos( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, p->pev->origin + p->pev->view_ofs );
	return 1;
}

static int Ply_EyeAngles( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, p->pev->v_angle );
	return 1;
}

static int Ply_Give( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	const char *cls = luaL_checkstring( L, 2 );

	edict_t *pent = NULL;
	if( XGLua_IsLuaWeapon( cls ) )
	{
		CBasePlayerItem *pItem = XGLua_GiveLuaWeapon( p, cls );	// Lua weapons bypass export-symbol lookup
		pent = pItem ? pItem->edict() : NULL;
	}
	else
	{
		// native entity — mirror GiveNamedItem
		pent = CREATE_NAMED_ENTITY( MAKE_STRING( cls ) );
		if( FNullEnt( pent ) )
		{
			luaL_error( L, "Give: unknown entity class '%s'", cls );
			return 0;
		}
		VARS( pent )->origin = p->pev->origin;
		pent->v.spawnflags |= SF_NORESPAWN;
		DispatchSpawn( pent );
		DispatchTouch( pent, ENT( p->pev ) );
	}

	if( !FNullEnt( pent ) )
		XGLua_PushEntity( L, pent );
	else
		lua_pushnil( L );
	return 1;
}

static int Ply_GiveAmmo( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	int count = (int)luaL_checknumber( L, 2 );
	const char *name = luaL_checkstring( L, 3 );
	int iMax = (int)luaL_optnumber( L, 4, 68 );
	p->GiveAmmo( count, const_cast<char *>( name ), iMax );
	return 0;
}

static int Ply_GetAmmoCount( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	const char *name = luaL_checkstring( L, 2 );
	int idx = p->GetAmmoIndex( name );
	lua_pushnumber( L, idx >= 0 ? p->m_rgAmmo[idx] : 0 );
	return 1;
}

static int Ply_PrintMessage( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	int dest = (int)luaL_checknumber( L, 2 );
	const char *msg = luaL_checkstring( L, 3 );
	// msg_name is a string table token; plain text passes through with no params
	ClientPrint( p->pev, dest, msg );
	return 0;
}

static int Ply_ChatPrint( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	const char *msg = luaL_checkstring( L, 2 );
	ClientPrint( p->pev, HUD_PRINTTALK, msg );
	return 0;
}

//===========================================================================
// ents / player / util libraries
//===========================================================================

static int ents_Create( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );

	edict_t *pent = NULL;
	if( XGLua_IsLuaWeapon( cls ) )
	{
		CBasePlayerItem *pItem = XGLua_SpawnLuaWeapon( cls, Vector( 0, 0, 0 ) );
		pent = pItem ? pItem->edict() : NULL;
	}
	else
	{
		pent = CREATE_NAMED_ENTITY( MAKE_STRING( cls ) );
	}
	if( FNullEnt( pent ) )
		luaL_error( L, "ents.Create: failed to create '%s'", cls );
	XGLua_PushEntity( L, pent );
	return 1;
}

static int ents_GetAll( lua_State *L )
{
	lua_newtable( L );
	int n = 0;
	for( int i = 0; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pent = INDEXENT( i );
		if( !pent || pent->free || !pent->pvPrivateData )
			continue;
		lua_pushnumber( L, ++n );
		XGLua_PushEntity( L, pent );
		lua_settable( L, -3 );
	}
	return 1;
}

static int ents_FindByClass( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	size_t len = strlen( cls );
	bool wildcard = len > 0 && cls[len - 1] == '*';

	lua_newtable( L );
	int n = 0;
	for( int i = 0; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pent = INDEXENT( i );
		if( !pent || pent->free || !pent->pvPrivateData )
			continue;
		const char *c = STRING( VARS( pent )->classname );
		if( !c || !c[0] )
			continue;
		if( wildcard )
		{
			if( strncmp( c, cls, len - 1 ) != 0 )
				continue;
		}
		else
		{
			if( strcmp( c, cls ) != 0 )
				continue;
		}
		lua_pushnumber( L, ++n );
		XGLua_PushEntity( L, pent );
		lua_settable( L, -3 );
	}
	return 1;
}

static int player_GetAll( lua_State *L )
{
	lua_newtable( L );
	int n = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if( !pEnt )
			continue;
		lua_pushnumber( L, ++n );
		XGLua_PushEntity( L, pEnt->edict() );
		lua_settable( L, -3 );
	}
	return 1;
}

static int Entity_Global( lua_State *L )
{
	int idx = (int)luaL_checknumber( L, 1 );
	edict_t *pent = INDEXENT( idx );
	if( !pent || pent->free )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, pent );
	return 1;
}

static int util_ThrowGrenade( lua_State *L )
{
	edict_t *pThrower = NULL;
	XGLua_OptEntity( L, 1, &pThrower );
	extern Vector XGLua_CheckVector( lua_State *L, int idx );
	Vector pos = XGLua_CheckVector( L, 2 );
	Vector vel = XGLua_CheckVector( L, 3 );
	BOOL contact = lua_toboolean( L, 4 );

	entvars_t *pevOwner = pThrower ? VARS( pThrower ) : NULL;
	CGrenade *pGrenade;
	if( contact )
		pGrenade = CGrenade::ShootContact( pevOwner, pos, vel );
	else
		pGrenade = CGrenade::ShootTimed( pevOwner, pos, vel, 3.0 );

	if( !pGrenade )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, pGrenade->edict() );
	return 1;
}

//===========================================================================
// registration
//===========================================================================

void XGLua_RegisterEntityLibs( lua_State *L )
{
	extern void XGLua_RegisterTypes( lua_State *L );
	XGLua_RegisterTypes( L );

	// Entity metatable — all methods live on it
	luaL_newmetatable( L, XGLUA_ENTITY_META );
	const luaL_Reg entmeta[] =
	{
		{ "IsValid", Ent_IsValid },
		{ "EntIndex", Ent_EntIndex },
		{ "GetClass", Ent_GetClass },
		{ "GetPos", Ent_GetPos },
		{ "SetPos", Ent_SetPos },
		{ "GetAngles", Ent_GetAngles },
		{ "SetAngles", Ent_SetAngles },
		{ "GetVelocity", Ent_GetVelocity },
		{ "SetVelocity", Ent_SetVelocity },
		{ "SetSolid", Ent_SetSolid },
		{ "SetMoveType", Ent_SetMoveType },
		{ "SetCollisionBounds", Ent_SetCollisionBounds },
		{ "GetModel", Ent_GetModel },
		{ "SetModel", Ent_SetModel },
		{ "Health", Ent_GetHealth },
		{ "SetHealth", Ent_SetHealth },
		{ "GetMaxHealth", Ent_GetMaxHealth },
		{ "SetMaxHealth", Ent_SetMaxHealth },
		{ "IsPlayer", Ent_IsPlayer },
		{ "GetOwner", Ent_GetOwner },
		{ "SetOwner", Ent_SetOwner },
		{ "GetGroundEntity", Ent_GetGroundEntity },
		{ "EmitSound", Ent_EmitSound },
		{ "StopSound", Ent_StopSound },
		{ "Spawn", Ent_Spawn },
		{ "Remove", Ent_Remove },
		{ "SetName", Ent_SetName },
		{ "GetName", Ent_GetName },
		{ "Nick", Ply_Nick },		// convenience: GMod allows on Entity too
		{ "Alive", Ply_Alive },
		{ "GetActiveWeapon", Ply_GetActiveWeapon },
		{ "GetAimVector", Ply_GetAimVector },
		{ "EyePos", Ply_EyePos },
		{ "EyeAngles", Ply_EyeAngles },
		{ "Give", Ply_Give },
		{ "GiveAmmo", Ply_GiveAmmo },
		{ "GetAmmoCount", Ply_GetAmmoCount },
		{ "PrintMessage", Ply_PrintMessage },
		{ "ChatPrint", Ply_ChatPrint },
		{ "Kill", Ply_Kill },
		{ "__index", Ent_Index },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = entmeta; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_pop( L, 1 );

	// ents library
	lua_newtable( L );
	const luaL_Reg entslib[] =
	{
		{ "Create", ents_Create },
		{ "GetAll", ents_GetAll },
		{ "FindByClass", ents_FindByClass },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = entslib; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "ents" );

	// player library
	lua_newtable( L );
	lua_pushcfunction( L, player_GetAll );
	lua_setfield( L, -2, "GetAll" );
	lua_setglobal( L, "player" );

	// util library
	lua_newtable( L );
	lua_pushcfunction( L, util_ThrowGrenade );
	lua_setfield( L, -2, "ThrowGrenade" );
	lua_setglobal( L, "util" );

	// Entity( index ) global constructor
	lua_pushcfunction( L, Entity_Global );
	lua_setglobal( L, "Entity" );

	// IsValid global
	lua_pushcfunction( L, Ent_IsValid );
	lua_setglobal( L, "IsValid" );
}
