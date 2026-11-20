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
#include "xglua_safe.h"
#include "xglua_vm_include.h" // XGLua_IncludePath / XGLUA_INCLUDE_PATH_MAX
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

#include "xglua_entity_handles.h"
edict_t *XGLua_TryEntity( lua_State *L, int idx )
{
	XGLuaEntRef *ref = XGLua_TestEntityRef( L, idx );
	return XGLua_ValidateEnt( ref );
}

#include "xglua_entity_spatial.h"

struct XGLua_EntityFrameCtx { int maxSlots; int *sweepIndex; };

static int XGLua_EntityFrameCore( lua_State *L, void *ctx )
{
	XGLua_EntityFrameCtx *frame = (XGLua_EntityFrameCtx *)ctx;
	XGLua_SweepEntityHandles( L, frame->maxSlots, 8, frame->sweepIndex );
	return 0;
}

void XGLua_EntityFrame( lua_State *L )
{
	// The cursor contains only a slot number; it is normalized on every call,
	// so retaining it between VMs cannot retain Lua state or entity pointers.
	static int sweepIndex = 0;
	if( !L || !gpGlobals ) return;
	XGLua_EntityFrameCtx frame = { gpGlobals->maxEntities, &sweepIndex };
	XGLua_ProtectedCall( L, XGLua_EntityFrameCore, &frame, XGLua_ReportError, "entity handle sweep" );
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
	XGLuaEntRef *ref = XGLua_TestEntityRef( L, idx );
	return ref && XGLua_ValidateEnt( ref ) != NULL;
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
	STOP_SOUND( pent, CHAN_ITEM, name );
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

static const char *XGLua_EntityClassName( edict_t *pent )
{
	return STRING( VARS( pent )->classname );
}

#include "xglua_entity_methods.h"

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

#include "xglua_entity_geometry.h"
#include "xglua_entity_state.h"
#include "xglua_player_api.h"
#include "xglua_player_extra.h"

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

static int Ply_ViewPunch( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	Vector angle = XGLua_CheckAngle( L, 2 );
	if( !std::isfinite( angle.x ) || !std::isfinite( angle.y ) || !std::isfinite( angle.z ) )
		return luaL_argerror( L, 2, "angle must be finite" );
	p->pev->punchangle = p->pev->punchangle + angle;
	return 0;
}

static int Ply_EyePos( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	extern void XGLua_PushVector( lua_State *L, const Vector &v );
	XGLua_PushVector( L, p->pev->origin + p->pev->view_ofs );
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

static int Ent_SetNextThink( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	double t = luaL_checknumber( L, 2 );
	if( !std::isfinite( t ) )
		return luaL_argerror( L, 2, "nextthink time must be a finite float" );
	VARS( pent )->nextthink = (float)t;
	return 0;
}

static int ents_Create( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );

	edict_t *pent = NULL;
	if( XGLua_IsLuaWeapon( cls ) )
	{
		CBasePlayerItem *pItem = XGLua_SpawnLuaWeapon( cls, Vector( 0, 0, 0 ) );
		pent = pItem ? pItem->edict() : NULL;
	}
	else if( XGLua_IsLuaEntity( cls ) )
	{
		CBaseEntity *pEnt = XGLua_SpawnLuaEntity( cls, Vector( 0, 0, 0 ) );
		pent = pEnt ? pEnt->edict() : NULL;
	}
	else if( XGLua_IsLuaNPC( cls ) )
	{
		CBaseEntity *pEnt = XGLua_SpawnLuaNPC( cls, Vector( 0, 0, 0 ) );
		pent = pEnt ? pEnt->edict() : NULL;
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

#include "xglua_util_trace.h"
#include "xglua_entity_queries.h"

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

static int XGLua_file_Exists( lua_State *L )
{
	const char *raw = luaL_checkstring( L, 1 );
	char clean[XGLUA_INCLUDE_PATH_MAX];
	if( !XGLua_IncludePath( NULL, raw, std::strlen( raw ), clean, sizeof( clean ) ) )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	bool exists = false;
	if( g_engfuncs.pfnGetFileSize )
		exists = g_engfuncs.pfnGetFileSize( clean ) > 0;
	else if( g_engfuncs.pfnLoadFileForMe && g_engfuncs.pfnFreeFile )
	{
		int len = 0;
		byte *data = LOAD_FILE_FOR_ME( clean, &len );
		if( data )
		{
			exists = len > 0;
			FREE_FILE( data );
		}
	}
	lua_pushboolean( L, exists ? 1 : 0 );
	return 1;
}

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
		{ "GetFeetPos", Ent_GetFeetPos },
		{ "WorldSpaceCenter", Ent_WorldSpaceCenter },
		{ "OBBMins", Ent_OBBMins },
		{ "OBBMaxs", Ent_OBBMaxs },
		{ "GetCollisionBounds", Ent_GetCollisionBounds },
		{ "SetPos", Ent_SetPos },
		{ "GetAngles", Ent_GetAngles },
		{ "SetAngles", Ent_SetAngles },
		{ "GetVelocity", Ent_GetVelocity },
		{ "SetVelocity", Ent_SetVelocity },
		{ "SetNextThink", Ent_SetNextThink },
		{ "NextThink", Ent_SetNextThink },
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
		{ "GetMoveType", Ent_GetMoveType },
		{ "GetSolid", Ent_GetSolid },
		{ "WaterLevel", Ent_WaterLevel },
		{ "OnGround", Ent_OnGround },
		{ "IsOnGround", Ent_OnGround },
		{ "Crouching", Ply_Crouching },
		{ "Armor", Ply_Armor },
		{ "Ping", Ply_Ping },
		{ "KeyDown", Ply_KeyDown },
		{ "KeyPressed", Ply_KeyPressed },
		{ "KeyReleased", Ply_KeyReleased },
		{ "EmitSound", Ent_EmitSound },
		{ "StopSound", Ent_StopSound },
		{ "Spawn", Ent_Spawn },
		{ "Remove", Ent_Remove },
		{ "SetName", Ent_SetName },
		{ "GetName", Ent_GetName },
		{ "Nick", Ply_Nick },		// convenience: GMod allows on Entity too
		{ "Name", Ply_Name },
		{ "Alive", Ply_Alive },
		{ "IsObserver", Ply_IsObserver },
		{ "GetFOV", Ply_GetFOV },
		{ "IsOnTrain", Ply_IsOnTrain },
		{ "IsUsing", Ply_IsUsing },
		{ "InVehicle", Ply_InVehicle },
		{ "IsFrozen", Ply_IsFrozen },
		{ "IsGodMode", Ply_IsGodMode },
		{ "Frags", Ply_Frags },
		{ "Deaths", Ply_Deaths },
		{ "Team", Ply_Team },
		{ "IsBot", Ply_IsBot },
		{ "IsConnected", Ply_IsConnected },
		{ "HasWeapons", Ply_HasWeapons },
		{ "GetWeapons", Ply_GetWeapons },
		{ "HasWeapon", Ply_HasWeapon },
		{ "GetActiveWeapon", Ply_GetActiveWeapon },
		{ "UserID", Ply_UserID },
		{ "GetAimVector", Ply_GetAimVector },
		{ "ViewPunch", Ply_ViewPunch },
		{ "EyePos", Ply_EyePos },
		{ "ShootPos", Ply_ShootPos },
		{ "EyeAngles", Ply_EyeAngles },
		{ "Give", Ply_Give },
		{ "GiveAmmo", Ply_GiveAmmo },
		{ "GetAmmoCount", Ply_GetAmmoCount },
		{ "PrintMessage", Ply_PrintMessage },
		{ "ChatPrint", Ply_ChatPrint },
		{ "Kill", Ply_Kill },
		{ "__index", Ent_Index },
		{ "__newindex", Ent_NewIndex },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = entmeta; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	// expose the real entity metatable as META_ENT so pure-Lua extensions
	// (nwvar.lua etc.) attach directly to entity userdata
	lua_pushvalue( L, -1 );
	lua_setglobal( L, "META_ENT" );
	lua_pop( L, 1 );

	// ents library
	lua_newtable( L );
	const luaL_Reg entslib[] =
	{
		{ "Create", ents_Create },
		{ "GetAll", ents_GetAll },
		{ "FindByClass", ents_FindByClass },
		{ "FindByName", ents_FindByName },
		{ "FindInSphere", ents_FindInSphere },
		{ "FindInBox", ents_FindInBox },
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
	lua_pushcfunction( L, player_GetBots );
	lua_setfield( L, -2, "GetBots" );
	lua_pushcfunction( L, player_GetHumans );
	lua_setfield( L, -2, "GetHumans" );
	lua_pushcfunction( L, player_GetByUserID );
	lua_setfield( L, -2, "GetByUserID" );
	lua_setglobal( L, "player" );

	// game library
	lua_newtable( L );
	lua_pushcfunction( L, game_GetMap );
	lua_setfield( L, -2, "GetMap" );
	lua_pushcfunction( L, game_MaxPlayers );
	lua_setfield( L, -2, "MaxPlayers" );
	lua_pushcfunction( L, game_GetWorld );
	lua_setfield( L, -2, "GetWorld" );
	lua_setglobal( L, "game" );

	// util library
	lua_newtable( L );
	lua_pushcfunction( L, util_ThrowGrenade );
	lua_setfield( L, -2, "ThrowGrenade" );
	lua_pushcfunction( L, util_TraceLine );
	lua_setfield( L, -2, "TraceLine" );
	lua_pushcfunction( L, util_TraceHull );
	lua_setfield( L, -2, "TraceHull" );
	lua_setglobal( L, "util" );

	// Entity( index ) global constructor
	lua_pushcfunction( L, Entity_Global );
	lua_setglobal( L, "Entity" );

	// IsValid global
	lua_pushcfunction( L, Ent_IsValid );
	lua_setglobal( L, "IsValid" );

	// file.Exists(relpath) — resource existence probe relative to the game
	// directory (used by addons to self-check optional model/sound assets)
	lua_newtable( L );
	lua_pushcfunction( L, XGLua_file_Exists );
	lua_setfield( L, -2, "Exists" );
	lua_setglobal( L, "file" );
}
