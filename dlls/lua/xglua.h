/***
*
*	XGLua — GMod-style Lua scripting for HL1 / Xash3D
*
*	Game-side glue. The Lua VM itself (LuaJIT) lives in the engine-level
*	lua_shared.dll (xglua-luajit repo); this code only binds game systems.
*
****/

#ifndef XGLUA_H
#define XGLUA_H

// lua_shared.dll headers live in the xglua-luajit external repo.
// LuaJIT headers have no extern "C" guard of their own - wrap them here.
extern "C" {
#include "lua.h"
}

#include "extdll.h"
#include "util.h"
#include "cbase.h"

class CBasePlayer;

//
// Lifecycle (hooked into the DLL_FUNCTIONS entry points with 1-line patches)
//
void XGLua_Init( void );                            // GameDLLInit: register console commands
void XGLua_Startup( void );                         // ServerActivate: create VM + load scripts
void XGLua_PostEntityInit( void );                  // ServerActivate: entity hooks
void XGLua_Shutdown( void );                        // ServerDeactivate: close VM
void XGLua_StartFrame( void );                      // StartFrame: timer.Tick + hook Think
void XGLua_PlayerSpawn( CBasePlayer *pPlayer );     // CBasePlayer::Spawn: hook PlayerSpawn
void XGLua_PlayerInitialSpawn( CBasePlayer *pPlayer ); // ClientPutInServer: hook PlayerInitialSpawn
BOOL XGLua_PlayerLoadout( CBasePlayer *pPlayer );   // gamerules PlayerSpawn: hook PlayerLoadout
void XGLua_PlayerDeath( CBasePlayer *victim, entvars_t *inflictor, entvars_t *attacker ); // CBasePlayer::Killed: hook PlayerDeath
void XGLua_PlayerDisconnected( CBasePlayer *pPlayer );               // ClientDisconnect: hook PlayerDisconnected

// ClientCommand say/say_team result for hook PlayerSay (xglua_vm.cpp)
enum XGLuaSayResult
{
	XGLUA_SAY_PASSTHROUGH = 0,	// no hook result / non-string / Lua error: native Host_Say with the original text
	XGLUA_SAY_REPLACED,		// hook returned a non-empty string: send that instead
	XGLUA_SAY_SWALLOWED		// hook returned "": drop the message entirely
};
XGLuaSayResult XGLua_PlayerSay( CBasePlayer *pPlayer, const char *text, int teamonly, char *out, size_t outlen );

// Pickup vetoes: FALSE only on a strict Lua false from the hook chain;
// every other outcome (true, nil, non-boolean, no handlers, Lua error)
// keeps the native allow behavior.
BOOL XGLua_PlayerCanPickupWeapon( CBasePlayer *pPlayer, CBaseEntity *pWeapon ); // CBasePlayerItem::AddToPlayer entry
BOOL XGLua_PlayerCanPickupItem( CBasePlayer *pPlayer, CBaseEntity *pItem );     // CItem::ItemTouch before MyTouch

// Damage / use events (xglua_damage.cpp; call graph in its header comment)
BOOL XGLua_PlayerUse( CBasePlayer *pPlayer, CBaseEntity *pTarget );                // PlayerUse: strict false blocks this interaction
BOOL XGLua_PlayerShouldTakeDamage( CBasePlayer *pPlayer, entvars_t *pevInflictor, entvars_t *pevAttacker ); // strict false exempts the player this hit
int XGLua_EntityTakeDamage( CBaseEntity *victim, entvars_t *pevInflictor, entvars_t *pevAttacker, float *pflDamage, int *pbitsType ); // 1 = cancel, 0 = continue (values may be modified)

//
// Scripted Entities (SENT - xglua_sent.cpp)
//
CBaseEntity *XGLua_SpawnLuaEntity( const char *classname, const Vector &origin );
BOOL XGLua_IsLuaEntity( const char *classname );
void XGLua_GetSENTRegistry( lua_State *L );
void XGLua_BuildEntityCache( lua_State *L );
void XGLua_ClearEntityCache( void );

//
// Script loading / dispatch
//
void XGLua_LoadScript( lua_State *L, const char *relpath );   // relative to <gamedir>/lua/
void XGLua_HookRun( lua_State *L, const char *event );        // hook.Run(event, ...)
// Error reporter for protected calls: logs "[XGLua] <context> error:"
// plus the message and pops the error object on top of the stack.
void XGLua_ReportError( lua_State *L, const char *context );

//
// Entities
//
void XGLua_EntityFrame( lua_State *L );                        // bounded entity cache cleanup
void XGLua_PushEntity( lua_State *L, edict_t *pent );         // entity handle userdata
edict_t *XGLua_CheckEntity( lua_State *L, int idx );          // throws if not entity
edict_t *XGLua_TryEntity( lua_State *L, int idx );            // returns NULL if not a valid entity
void XGLua_RegisterEntityLibs( lua_State *L );                // ents/player/util/Entity metatable

//
// Weapons (SWEP)
//
class CBasePlayerItem;
CBasePlayerItem *XGLua_SpawnLuaWeapon( const char *classname, const Vector &origin );
BOOL XGLua_IsLuaWeapon( const char *classname );
void XGLua_GetSWEPRegistry( lua_State *L );              // registry table access
void XGLua_RegisterWeaponLibs( lua_State *L );                // weapon entity methods
void XGLua_BuildWeaponCache( lua_State *L );                  // SWEP tables -> C++ cache
CBasePlayerItem *XGLua_GiveLuaWeapon( CBasePlayer *pPlayer, const char *classname );
bool XGLua_IsTrackedWeapon( const edict_t *pent );
void XGLua_ClearWeaponTracking();
void XGLua_PreparePlayerSave( CBasePlayer *player );
void XGLua_MarkRestoredPlayer( CBasePlayer *player );
void XGLua_ReconcileRestoredPlayers( edict_t *list, int count, int clients );
BOOL XGLua_CanPickupSecondary( CBasePlayer *player, CBasePlayerItem *item );

//
// Types (Vector / Angle)
//
void XGLua_RegisterTypes( lua_State *L );
void XGLua_PushVector( lua_State *L, const Vector &v );
Vector XGLua_CheckVector( lua_State *L, int idx );
Vector XGLua_CheckAngle( lua_State *L, int idx );

//
// Shared VM pointer (NULL when no scripts loaded / shutdown)
//
extern lua_State *g_xglua_L;

#endif // XGLUA_H
