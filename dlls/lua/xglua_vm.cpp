/***
*
*	XGLua — VM lifecycle, script loading, base globals, hook dispatch
*
*	Realm: SERVER. One VM per map, created at ServerActivate, closed at
*	ServerDeactivate (same lifecycle as GMod's Lua state on map change).
*
****/

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "xglua.h"
#include "xglua_safe.h"
#include "xglua_hook.h"
#include "xglua_vm_timer.h"
#include "player.h"
extern "C" {
#include "lualib.h"
#include "lauxlib.h"
}
#include "xglua_vm_include.h"
// M4 first batch: server gamemode chain (header-only, this is the only TU)
#include "xglua_gamemode.h"

lua_State *g_xglua_L = NULL;

static char g_szGameDir[256] = "";

// registry key: { [classname] = SWEP table } — shared with xglua_weapon.cpp
#define XGLUA_SWEP_REGISTRY "XGLua_SWEPTables"

void XGLua_GetSWEPRegistry( lua_State *L );

//===========================================================================
// helpers
//===========================================================================

// Shared engine-side error reporter for protected calls: receives the error
// object on top and pops it. Declared in xglua.h so other glue files
// dispatch through the same protected-call boundary (xglua_safe.h).
void XGLua_ReportError( lua_State *L, const char *context )
{
	// The reporter runs after the protected call has returned, so no string
	// conversion is allowed here: only genuine strings are printed, any
	// other error value degrades to its type name (XGLua_ErrorText).
	ALERT( at_console, "[XGLua] %s error:\n%s\n", context, XGLua_ErrorText( L, -1 ) );
	lua_pop( L, 1 );
}

void XGLua_HookRun( lua_State *L, const char *event )
{
	// hook.Run( event ) - lookup and call inside one protected frame
	XGLua_HookRunProtected( L, event, NULL, NULL, XGLua_ReportError );
}

// Internal loads discard script results but retain/report failures. Includes
// use a separate Lua-facing wrapper so their results and errors propagate.
static bool XGLua_LoadScriptChecked( lua_State *L, const char *relpath )
{
    const int top = lua_gettop( L );
    char relative[XGLUA_INCLUDE_PATH_MAX];
    if( !relpath || !XGLua_IncludePath( NULL, relpath, strlen( relpath ), relative, sizeof( relative ) ) )
    {
        ALERT( at_console, "[XGLua] invalid script path\n" );
        return false;
    }
    int status = XGLua_ExecuteScript( L, relative, XGLua_ReadDiskScript, g_szGameDir, 0 );
    if( status != XGLUA_READ_OK )
    {
        XGLua_ReportError( L, "script load" );
        lua_settop( L, top );
        return false;
    }
    lua_settop( L, top );
    return true;
}

void XGLua_LoadScript( lua_State *L, const char *relpath )
{
    XGLua_LoadScriptChecked( L, relpath );
}

static void XGLua_LoadFolder( lua_State *L, const char *relfolder, bool recursive, const char *presetGlobal = NULL )
{
	// scan <gamedir>/lua/<relfolder>/*.lua (alphabetical on NTFS)
	char szDir[512];
	snprintf( szDir, sizeof( szDir ), "%s/lua/%s/", g_szGameDir, relfolder );

	char szMask[600];
	snprintf( szMask, sizeof( szMask ), "%s*.lua", szDir );

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( szMask, &fd );
	if( h != INVALID_HANDLE_VALUE )
	{
		do
		{
			char szRel[512];
			if( relfolder[0] )
				snprintf( szRel, sizeof( szRel ), "%s/%s", relfolder, fd.cFileName );
			else
				snprintf( szRel, sizeof( szRel ), "%s", fd.cFileName );

			if( presetGlobal )
			{
				lua_newtable( L );
				lua_setglobal( L, presetGlobal );	// fresh SWEP table per file (GMod contract)
			}
			XGLua_LoadScript( L, szRel );
		} while( FindNextFileA( h, &fd ) );
		FindClose( h );
	}

	if( !recursive )
		return;

	// recurse into subdirectories (autorun/server etc.)
	snprintf( szMask, sizeof( szMask ), "%s*", szDir );
	h = FindFirstFileA( szMask, &fd );
	if( h == INVALID_HANDLE_VALUE )
		return;
	do
	{
		if( !( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			continue;
		if( fd.cFileName[0] == '.' )
			continue;

		char szSub[512];
		if( relfolder[0] )
			snprintf( szSub, sizeof( szSub ), "%s/%s", relfolder, fd.cFileName );
		else
			snprintf( szSub, sizeof( szSub ), "%s", fd.cFileName );
		XGLua_LoadFolder( L, szSub, recursive );
	} while( FindNextFileA( h, &fd ) );
	FindClose( h );
}

//===========================================================================
// base globals
//===========================================================================

static int XGLua_print( lua_State *L )
{
	int n = lua_gettop( L );
	std::string out;
	for( int i = 1; i <= n; i++ )
	{
		size_t len;
		const char *s = lua_tolstring( L, i, &len );
		if( s )
			out.append( s, len );
		else
			out += lua_typename( L, lua_type( L, i ) );
		if( i < n )
			out += "  ";
	}
	ALERT( at_console, "[Lua] %s\n", out.c_str() );
	return 0;
}

static int XGLua_CurTime( lua_State *L )
{
	lua_pushnumber( L, gpGlobals->time );
	return 1;
}

static int XGLua_RealTime( lua_State *L )
{
	lua_pushnumber( L, (double)clock() / CLOCKS_PER_SEC );
	return 1;
}

static int XGLua_FrameTime( lua_State *L )
{
	lua_pushnumber( L, gpGlobals->frametime );
	return 1;
}

static int XGLua_include( lua_State *L )
{
	const int n = XGLua_IncludeImpl( L, XGLua_ReadDiskScript, g_szGameDir );
	if( n < 0 ) return lua_error( L );
	return n;
}

//===========================================================================
// lua_run console command
//===========================================================================

static void XGLua_LuaRun_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	std::string code;
	for( int i = 1; i < CMD_ARGC(); i++ )
	{
		if( i > 1 )
			code += " ";
		code += CMD_ARGV( i );
	}
	if( code.empty() )
	{
		ALERT( at_console, "usage: lua_run <code>\n" );
		return;
	}

	lua_State *L = g_xglua_L;
	// luaL_loadbuffer is protected internally (lj_load.c lua_loadx uses
	// lj_vm_cpcall): syntax and OOM errors come back as a status code.
	if( luaL_loadbuffer( L, code.c_str(), code.size(), "lua_run" ) != 0 )
	{
		XGLua_ReportError( L, "lua_run load" );
		return;
	}
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		XGLua_ReportError( L, "lua_run" );
		return;
	}
	// lua_isstring also matches numbers, and converting one allocates;
	// only genuine strings go through lua_tostring, numbers echo via %g.
	if( lua_type( L, -1 ) == LUA_TSTRING )
		ALERT( at_console, "[Lua] %s\n", lua_tostring( L, -1 ) );
	else if( lua_type( L, -1 ) == LUA_TNUMBER )
	{
		char szNum[32];
		snprintf( szNum, sizeof( szNum ), "%g", lua_tonumber( L, -1 ) );
		ALERT( at_console, "[Lua] %s\n", szNum );
	}
	lua_pop( L, 1 );
}

//===========================================================================
// SWEP registry helpers
//===========================================================================

void XGLua_GetSWEPRegistry( lua_State *L )
{
	// registry[XGLUA_SWEP_REGISTRY] = { [classname] = SWEP table }
	// Raw access on purpose: the registry is engine-internal state and this
	// helper also runs on the engine Restore path, where a script-planted
	// registry metatable must not turn a lookup into an unprotected raise.
	lua_pushlightuserdata( L, (void *)XGLUA_SWEP_REGISTRY );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_newtable( L );
		lua_pushvalue( L, -1 );						// copy
		lua_pushlightuserdata( L, (void *)XGLUA_SWEP_REGISTRY );
		lua_insert( L, -2 );						// key under value
		lua_rawset( L, LUA_REGISTRYINDEX );		// registry[key] = table
	}
}

struct XGLua_IsLuaWeaponCtx { const char *classname; BOOL found; };

static int XGLua_IsLuaWeaponCore( lua_State *L, void *ud )
{
	XGLua_IsLuaWeaponCtx *ctx = (XGLua_IsLuaWeaponCtx *)ud;
	XGLua_GetSWEPRegistry( L );
	lua_pushstring( L, ctx->classname );
	lua_rawget( L, -2 );
	ctx->found = lua_istable( L, -1 ) ? TRUE : FALSE;
	return 0;
}

BOOL XGLua_IsLuaWeapon( const char *classname )
{
	lua_State *L = g_xglua_L;
	if( !L || !classname )
		return FALSE;

	// This lookup also runs from native Give/restore engine callbacks. The
	// registry access is raw, but pushing an unregistered classname can still
	// allocate, so keep the entire operation inside the protected boundary.
	XGLua_IsLuaWeaponCtx ctx = { classname, FALSE };
	XGLua_ProtectedCall( L, XGLua_IsLuaWeaponCore, &ctx, XGLua_ReportError, "weapon registry lookup" );
	return ctx.found;
}

//===========================================================================
// startup / shutdown
//===========================================================================

// gamemode selection (design doc 3.3): xgl_gamemode "<name>" in server.cfg;
// empty means no gamemode and keeps the plain startup behavior
static cvar_t xgl_gamemode = { "xgl_gamemode", "" };

void XGLua_Init( void )
{
	g_engfuncs.pfnAddServerCommand( "lua_run", XGLua_LuaRun_f );
	CVAR_REGISTER( &xgl_gamemode );
}

void XGLua_Startup( void )
{
	if( g_xglua_L )
		return;

	// Resolve the lua root. Engine GET_GAME_DIR behavior differs between
	// builds: bare folder name ("valve"), full path, or root dir with a
	// trailing separator. Probe candidates for lua/includes/init.lua.
	static const char *SEP = "/";
	char szCand[512];
	const char *szChosen = NULL;

	GET_GAME_DIR( g_szGameDir );

	// strip trailing separators
	size_t nLen = strlen( g_szGameDir );
	while( nLen > 0 && ( g_szGameDir[nLen - 1] == '/' || g_szGameDir[nLen - 1] == '\\' ) )
		g_szGameDir[--nLen] = '\0';

	const char *szExeDir = NULL;
	char szExe[512];
	if( GetModuleFileNameA( NULL, szExe, sizeof( szExe ) ) )
	{
		char *pLast = strrchr( szExe, '\\' );
		if( !pLast )
			pLast = strrchr( szExe, '/' );
		if( pLast )
			*pLast = '\0';
		szExeDir = szExe;
	}

	for( int attempt = 0; attempt < 4 && !szChosen; attempt++ )
	{
		switch( attempt )
		{
		case 0:	// as reported
			snprintf( szCand, sizeof( szCand ), "%s", g_szGameDir );
			break;
		case 1:	// exe dir + reported name (bare "valve" case)
			if( !szExeDir )
				continue;
			snprintf( szCand, sizeof( szCand ), "%s%s%s", szExeDir, SEP, g_szGameDir );
			break;
		case 2:	// reported dir + valve (root dir case)
			snprintf( szCand, sizeof( szCand ), "%s%svalve", g_szGameDir, SEP );
			break;
		case 3:	// exe dir + valve
			if( !szExeDir )
				continue;
			snprintf( szCand, sizeof( szCand ), "%s%svalve", szExeDir, SEP );
			break;
		}

		char szProbe[600];
		snprintf( szProbe, sizeof( szProbe ), "%s%slua%sincludes%sinit.lua", szCand, SEP, SEP, SEP );
		FILE *fp = fopen( szProbe, "rb" );
		if( fp )
		{
			fclose( fp );
			szChosen = szCand;
			strncpy( g_szGameDir, szCand, sizeof( g_szGameDir ) - 1 );
		}
	}

	if( !szChosen )
	{
		ALERT( at_console, "[XGLua] FAILED to locate lua/includes/init.lua, VM not started\n" );
		return;
	}
	ALERT( at_console, "[XGLua] starting Lua VM (lua root: %s)\n", g_szGameDir );

	lua_State *L = luaL_newstate();
	if( !L )
	{
		ALERT( at_console, "[XGLua] FAILED to create Lua state\n" );
		return;
	}
	g_xglua_L = L;

	luaL_openlibs( L );

	// Everything up to and including the base globals runs before any user
	// script, so no user metamethods can exist on _G or package yet and these
	// writes cannot raise. Script-loaded state below is only touched inside
	// protected calls (see xglua_safe.h).
	// package.path — lua/ tree
	char szPath[1024];
	snprintf( szPath, sizeof( szPath ),
		"%s/lua/includes/?.lua;%s/lua/includes/?/init.lua;%s/lua/?.lua",
		g_szGameDir, g_szGameDir, g_szGameDir );
	lua_getglobal( L, "package" );
	lua_pushstring( L, szPath );
	lua_setfield( L, -2, "path" );
	lua_pop( L, 1 );

	// realm flags
	lua_pushboolean( L, 1 );
	lua_setglobal( L, "SERVER" );
	lua_pushboolean( L, 0 );
	lua_setglobal( L, "CLIENT" );

	// base globals
	const luaL_Reg baseglobals[] =
	{
		{ "print", XGLua_print },
		{ "CurTime", XGLua_CurTime },
		{ "RealTime", XGLua_RealTime },
		{ "FrameTime", XGLua_FrameTime },
		{ "include", XGLua_include },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = baseglobals; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setglobal( L, f->name );
	}

	// game libraries
	XGLua_RegisterEntityLibs( L );
	XGLua_RegisterWeaponLibs( L );

	// 1. core Lua layer (init.lua loads modules + extensions + enum)
	if( !XGLua_RunCoreInit( &g_xglua_L, L, XGLua_ReadDiskScript, g_szGameDir, XGLua_ReportError ) )
	{
		ALERT( at_console, "[XGLua] core init failed, VM not started\n" );
		return;
	}

	// 2. gamemode chain (design doc 4.2: includes -> gamemode -> autorun).
	// Selected via xgl_gamemode; base chain from gamemodes/<n>/<n>.txt;
	// shared.lua then init.lua per mode, base first. Installs GM/GAMEMODE
	// before autorun so hook.Run already has its gamemode fallback. Any
	// failure is diagnosed and skipped - the server keeps the plain behavior.
	{
		const char *szMode = CVAR_GET_STRING( "xgl_gamemode" );
		if( szMode && szMode[0] )
		{
			char szGMError[256];
			XGLua_GMResult gm = XGLua_LoadGamemodeChain( L, XGLua_ReadDiskRoot, g_szGameDir,
				szMode, szGMError, sizeof( szGMError ) );
			if( gm == XGLUA_GM_OK )
				ALERT( at_console, "[XGLua] gamemode loaded: %s\n", szMode );
			else
				ALERT( at_console, "[XGLua] gamemode '%s' not loaded (%d): %s\n", szMode, (int)gm, szGMError );
		}
	}

	// 3. autorun (top level, then server/ recursively)
	XGLua_LoadFolder( L, "autorun", false );
	XGLua_LoadFolder( L, "autorun/server", true );

	// 4. weapons — each file builds the global SWEP table
	XGLua_LoadFolder( L, "weapons", false, "SWEP" );

	// 4b. snapshot SWEP tables into the C++ cache (ItemInfoArray etc.)
	XGLua_BuildWeaponCache( L );

	ALERT( at_console, "[XGLua] scripts loaded, waiting for ServerActivate\n" );
}

void XGLua_PostEntityInit( void )
{
	if( !g_xglua_L ) return;
	XGLua_HookRun( g_xglua_L, "Initialize" );
	XGLua_HookRun( g_xglua_L, "InitPostEntity" );
	ALERT( at_console, "[XGLua] scripts loaded, VM ready\n" );
}

void XGLua_Shutdown( void )
{
	XGLua_ClearWeaponTracking();
	if( !g_xglua_L )
		return;

	XGLua_HookRun( g_xglua_L, "ShutDown" );

	ALERT( at_console, "[XGLua] shutting down VM\n" );
	lua_close( g_xglua_L );
	g_xglua_L = NULL;
}

void XGLua_StartFrame( void )
{
	lua_State *L = g_xglua_L;
	if( !L )
		return;

	XGLua_EntityFrame( L );  // bounded cleanup of native-removed handles
	XGLua_RunTimerTick( L, XGLua_ReportError );

	XGLua_HookRun( L, "Think" );
}

static int XGLua_PushSpawnedPlayer( lua_State *L, void *ctx )
{
	XGLua_PushEntity( L, ( (CBasePlayer *)ctx )->edict() );
	return 1;
}

void XGLua_PlayerSpawn( CBasePlayer *pPlayer )
{
	// hook.Run( "PlayerSpawn", ply ) via the shared protected dispatch;
	// the entity handle is pushed inside the protected frame
	XGLua_HookRunProtected( g_xglua_L, "PlayerSpawn", XGLua_PushSpawnedPlayer, pPlayer, XGLua_ReportError );
}
