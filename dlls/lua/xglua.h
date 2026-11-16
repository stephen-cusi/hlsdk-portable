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
void XGLua_Shutdown( void );                        // ServerDeactivate: close VM
void XGLua_StartFrame( void );                      // StartFrame: timer.Tick + hook Think
void XGLua_PlayerSpawn( CBasePlayer *pPlayer );     // CBasePlayer::Spawn: hook PlayerSpawn

//
// Script loading / dispatch
//
void XGLua_LoadScript( lua_State *L, const char *relpath );   // relative to <gamedir>/lua/
void XGLua_HookRun( lua_State *L, const char *event );        // hook.Run(event, ...)

//
// Entities
//
void XGLua_PushEntity( lua_State *L, edict_t *pent );         // entity handle userdata
edict_t *XGLua_CheckEntity( lua_State *L, int idx );          // throws if not entity
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

//
// Types (Vector / Angle)
//
void XGLua_RegisterTypes( lua_State *L );
void XGLua_PushVector( lua_State *L, const Vector &v );
Vector XGLua_CheckVector( lua_State *L, int idx );

//
// Shared VM pointer (NULL when no scripts loaded / shutdown)
//
extern lua_State *g_xglua_L;

#endif // XGLUA_H
