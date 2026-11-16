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
#include "player.h"
extern "C" {
#include "lualib.h"
#include "lauxlib.h"
}

lua_State *g_xglua_L = NULL;

static char g_szGameDir[256] = "";

// registry key: { [classname] = SWEP table } — shared with xglua_weapon.cpp
#define XGLUA_SWEP_REGISTRY "XGLua_SWEPTables"

void XGLua_GetSWEPRegistry( lua_State *L );

//===========================================================================
// helpers
//===========================================================================

static void LuaError( lua_State *L, const char *context )
{
	ALERT( at_console, "[XGLua] %s error:\n%s\n", context, lua_tostring( L, -1 ) );
	lua_pop( L, 1 );
}

void XGLua_HookRun( lua_State *L, const char *event )
{
	if( !L )
		return;

	// hook.Run( event )
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return;
	}
	lua_pushstring( L, event );
	lua_remove( L, -3 );	// drop hook table

	if( lua_pcall( L, 1, 0, 0 ) != 0 )
	{
		char szCtx[128];
		snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
		LuaError( L, szCtx );
	}
}

void XGLua_LoadScript( lua_State *L, const char *relpath )
{
	char szPath[512];
	snprintf( szPath, sizeof( szPath ), "%s/lua/%s", g_szGameDir, relpath );

	FILE *fp = fopen( szPath, "rb" );
	if( !fp )
	{
		ALERT( at_console, "[XGLua] script not found: %s\n", szPath );
		return;
	}

	fseek( fp, 0, SEEK_END );
	long nSize = ftell( fp );
	rewind( fp );
	if( nSize <= 0 )
	{
		fclose( fp );
		return;
	}
	char *pszData = (char *)malloc( nSize + 1 );
	long nRead = (long)fread( pszData, 1, nSize, fp );
	fclose( fp );
	pszData[nRead] = '\0';

	// skip UTF-8 BOM if present
	const char *pszChunk = pszData;
	long nChunk = nRead;
	if( nChunk >= 3 && (unsigned char)pszChunk[0] == 0xEF && (unsigned char)pszChunk[1] == 0xBB )
	{
		pszChunk += 3;
		nChunk -= 3;
	}

	if( luaL_loadbuffer( L, pszChunk, nChunk, relpath ) != 0 )
	{
		LuaError( L, "load" );
		free( pszData );
		return;
	}
	free( pszData );

	if( lua_pcall( L, 0, 0, 0 ) != 0 )
	{
		char szCtx[256];
		snprintf( szCtx, sizeof( szCtx ), "run %s", relpath );
		LuaError( L, szCtx );
	}
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
	const char *path = luaL_checkstring( L, 1 );
	XGLua_LoadScript( L, path );
	return 0;
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
	if( luaL_loadbuffer( L, code.c_str(), code.size(), "lua_run" ) != 0 )
	{
		LuaError( L, "lua_run load" );
		return;
	}
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		LuaError( L, "lua_run" );
		return;
	}
	if( lua_isstring( L, -1 ) )
		ALERT( at_console, "[Lua] %s\n", lua_tostring( L, -1 ) );
	lua_pop( L, 1 );
}

//===========================================================================
// SWEP registry helpers
//===========================================================================

void XGLua_GetSWEPRegistry( lua_State *L )
{
	// registry[XGLUA_SWEP_REGISTRY] = { [classname] = SWEP table }
	lua_pushlightuserdata( L, (void *)XGLUA_SWEP_REGISTRY );
	lua_gettable( L, LUA_REGISTRYINDEX );
	if( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_newtable( L );
		lua_pushvalue( L, -1 );						// copy
		lua_pushlightuserdata( L, (void *)XGLUA_SWEP_REGISTRY );
		lua_insert( L, -2 );						// key under value
		lua_settable( L, LUA_REGISTRYINDEX );		// registry[key] = table
	}
}

BOOL XGLua_IsLuaWeapon( const char *classname )
{
	lua_State *L = g_xglua_L;
	if( !L )
		return FALSE;

	XGLua_GetSWEPRegistry( L );
	lua_pushstring( L, classname );
	lua_rawget( L, -2 );
	BOOL bFound = lua_istable( L, -1 );
	lua_pop( L, 2 );
	return bFound;
}

//===========================================================================
// startup / shutdown
//===========================================================================

void XGLua_Init( void )
{
	g_engfuncs.pfnAddServerCommand( "lua_run", XGLua_LuaRun_f );
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
	XGLua_LoadScript( L, "includes/init.lua" );

	// 2. autorun (top level, then server/ recursively)
	XGLua_LoadFolder( L, "autorun", false );
	XGLua_LoadFolder( L, "autorun/server", true );

	// 3. weapons — each file builds the global SWEP table
	XGLua_LoadFolder( L, "weapons", false, "SWEP" );

	// 3b. snapshot SWEP tables into the C++ cache (ItemInfoArray etc.)
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

	// timer.Tick() (per-frame timer driver, defined in Lua)
	lua_getglobal( L, "timer" );
	if( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Tick" );
		if( lua_isfunction( L, -1 ) )
		{
			if( lua_pcall( L, 0, 0, 0 ) != 0 )
				LuaError( L, "timer.Tick" );
		}
		else
			lua_pop( L, 1 );
	}
	else
		lua_pop( L, 1 );

	XGLua_HookRun( L, "Think" );
}

void XGLua_PlayerSpawn( CBasePlayer *pPlayer )
{
	lua_State *L = g_xglua_L;
	if( !L || !pPlayer )
		return;

	lua_getglobal( L, "hook" );
	lua_getfield( L, -1, "Run" );
	lua_remove( L, -2 );
	lua_pushstring( L, "PlayerSpawn" );
	XGLua_PushEntity( L, pPlayer->edict() );
	if( lua_pcall( L, 2, 0, 0 ) != 0 )
		LuaError( L, "hook.Run(PlayerSpawn)" );
}
