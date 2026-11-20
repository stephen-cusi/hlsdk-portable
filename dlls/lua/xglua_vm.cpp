/***
*
*	XGLua — VM lifecycle, script loading, base globals, hook dispatch
*
*	Realm: SERVER. One VM per map, created at ServerActivate, closed at
*	ServerDeactivate (same lifecycle as GMod's Lua state on map change).
*
****/

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include "xglua.h"
#include "xglua_safe.h"
#include "xglua_hook.h"
#include "xglua_vm_timer.h"
#include "lua/xglua_dev.h"
#include "player.h"
#include "gamerules.h"
extern "C" {
#include "lualib.h"
#include "lauxlib.h"
}
#include "xglua_vm_include.h"
#include "xglua_vm_vfs.h"
// M4 first batch: server gamemode chain (header-only, this is the only TU)
#include "xglua_gamemode.h"
#include "xglua_addon_load.h"

lua_State *g_xglua_L = NULL;

#include "xglua_console.h"

static char g_szGameDir[256] = "";

// registry key: { [classname] = SWEP table } — shared with xglua_weapon.cpp
#define XGLUA_SWEP_REGISTRY "XGLua_SWEPTables"

void XGLua_GetSWEPRegistry( lua_State *L );

static XGLuaReadStatus XGLua_ReadEngineScript( void *ctx, const char *rel, char **out, size_t *len )
{
	if( !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile )
		return XGLua_ReadDiskScript( ctx, rel, out, len );
	return XGLua_ReadVFSFile( "lua/", rel, g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile, out, len );
}

static XGLuaReadStatus XGLua_ReadEngineRoot( void *ctx, const char *rel, char **out, size_t *len )
{
	if( !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile )
		return XGLua_ReadDiskRoot( ctx, rel, out, len );
	return XGLua_ReadVFSFile( "", rel, g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile, out, len );
}

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
    int status = XGLua_ExecuteScript( L, relative, XGLua_ReadEngineScript, g_szGameDir, 0 );
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

static bool XGLua_EndsWithLua( const char *name )
{
	if( !name ) return false;
	size_t len = strlen( name );
	if( len <= 4 ) return false;
	const char *ext = name + len - 4;
	return ext[0] == '.' &&
		( ext[1] == 'l' || ext[1] == 'L' ) &&
		( ext[2] == 'u' || ext[2] == 'U' ) &&
		( ext[3] == 'a' || ext[3] == 'A' );
}

static void XGLua_LoadFolder( lua_State *L, const char *relfolder, bool recursive, const char *presetGlobal = NULL )
{
	char szDir[512];
	snprintf( szDir, sizeof( szDir ), "%s/lua/%s/", g_szGameDir, relfolder );

	std::vector<std::string> luaFiles;
	std::vector<std::string> subDirs;

#ifdef _WIN32
	char szMask[600];
	snprintf( szMask, sizeof( szMask ), "%s*", szDir );
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( szMask, &fd );
	if( h != INVALID_HANDLE_VALUE )
	{
		do
		{
			if( fd.cFileName[0] == '.' )
				continue;
			if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				// Do not traverse junctions or directory symlinks.
				if( !( fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) )
					subDirs.push_back( fd.cFileName );
			}
			else if( XGLua_EndsWithLua( fd.cFileName ) )
				luaFiles.push_back( fd.cFileName );
		} while( FindNextFileA( h, &fd ) );
		FindClose( h );
	}
#else
	DIR *dir = opendir( szDir );
	if( dir )
	{
		struct dirent *ent;
		while( ( ent = readdir( dir ) ) != NULL )
		{
			if( ent->d_name[0] == '.' )
				continue;
			char fullPath[600];
			snprintf( fullPath, sizeof( fullPath ), "%s%s", szDir, ent->d_name );
			struct stat st;
			if( lstat( fullPath, &st ) != 0 )
				continue;
			if( S_ISLNK( st.st_mode ) )
			{
				// Permit linked scripts but never recurse through linked directories.
				if( stat( fullPath, &st ) != 0 || !S_ISREG( st.st_mode ) )
					continue;
			}
			if( S_ISDIR( st.st_mode ) )
				subDirs.push_back( ent->d_name );
			else if( S_ISREG( st.st_mode ) && XGLua_EndsWithLua( ent->d_name ) )
				luaFiles.push_back( ent->d_name );
		}
		closedir( dir );
	}
#endif

	std::sort( luaFiles.begin(), luaFiles.end() );
	for( size_t i = 0; i < luaFiles.size(); ++i )
	{
		char szRel[512];
		if( relfolder[0] )
			snprintf( szRel, sizeof( szRel ), "%s/%s", relfolder, luaFiles[i].c_str() );
		else
			snprintf( szRel, sizeof( szRel ), "%s", luaFiles[i].c_str() );

		if( presetGlobal )
		{
			lua_newtable( L );
			lua_setglobal( L, presetGlobal );
		}
		XGLua_LoadScript( L, szRel );
	}

	if( !recursive )
		return;

	std::sort( subDirs.begin(), subDirs.end() );
	for( size_t i = 0; i < subDirs.size(); ++i )
	{
		char szSub[512];
		if( relfolder[0] )
			snprintf( szSub, sizeof( szSub ), "%s/%s", relfolder, subDirs[i].c_str() );
		else
			snprintf( szSub, sizeof( szSub ), "%s", subDirs[i].c_str() );
		XGLua_LoadFolder( L, szSub, recursive, presetGlobal );
	}
}

//===========================================================================
// scripted entities — lua/entities/<name>.lua and <name>/init.lua
//===========================================================================

struct XGLua_AutoRegEntCtx { const char *classname; };

static int XGLua_AutoRegisterEntityCore( lua_State *L, void *ud )
{
	XGLua_AutoRegEntCtx *ctx = (XGLua_AutoRegEntCtx *)ud;
	lua_getglobal( L, "ENT" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getglobal( L, "scripted_ents" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Get" );
	if( lua_isfunction( L, -1 ) )
	{
		lua_pushstring( L, ctx->classname );
		if( lua_pcall( L, 1, 1, 0 ) != 0 )
			return lua_error( L );
		const bool already = lua_istable( L, -1 ) != 0;
		lua_pop( L, 1 );
		if( already )
			return 0;
	}
	else
	{
		lua_pop( L, 1 );
	}
	lua_getfield( L, -1, "Register" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushvalue( L, -3 ); // ENT table
	lua_pushstring( L, ctx->classname );
	if( lua_pcall( L, 2, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

static void XGLua_LoadOneEntityFile( lua_State *L, const char *relpath, const char *classname )
{
	lua_newtable( L );
	lua_setglobal( L, "ENT" );
	if( XGLua_LoadScriptChecked( L, relpath ) )
	{
		XGLua_AutoRegEntCtx ctx = { classname };
		XGLua_ProtectedCall( L, XGLua_AutoRegisterEntityCore, &ctx, XGLua_ReportError, "scripted_ents auto-register" );
	}
	lua_pushnil( L );
	lua_setglobal( L, "ENT" );
}

static void XGLua_LoadEntitiesFolder( lua_State *L )
{
	char szDir[512];
	snprintf( szDir, sizeof( szDir ), "%s/lua/entities/", g_szGameDir );

	std::vector<std::string> dirEntries;
	std::vector<std::string> fileEntries;

#ifdef _WIN32
	char szMask[600];
	snprintf( szMask, sizeof( szMask ), "%s*", szDir );
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( szMask, &fd );
	if( h != INVALID_HANDLE_VALUE )
	{
		do
		{
			if( fd.cFileName[0] == '.' )
				continue;
			if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				// Do not traverse junctions or directory symlinks.
				if( !( fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) )
					dirEntries.push_back( fd.cFileName );
			}
			else if( XGLua_EndsWithLua( fd.cFileName ) )
				fileEntries.push_back( fd.cFileName );
		} while( FindNextFileA( h, &fd ) );
		FindClose( h );
	}
#else
	DIR *dir = opendir( szDir );
	if( dir )
	{
		struct dirent *ent;
		while( ( ent = readdir( dir ) ) != NULL )
		{
			if( ent->d_name[0] == '.' )
				continue;
			char fullPath[600];
			snprintf( fullPath, sizeof( fullPath ), "%s%s", szDir, ent->d_name );
			struct stat st;
			if( lstat( fullPath, &st ) != 0 )
				continue;
			if( S_ISLNK( st.st_mode ) )
			{
				// Permit linked scripts but never recurse through linked directories.
				if( stat( fullPath, &st ) != 0 || !S_ISREG( st.st_mode ) )
					continue;
			}
			if( S_ISDIR( st.st_mode ) )
				dirEntries.push_back( ent->d_name );
			else if( S_ISREG( st.st_mode ) && XGLua_EndsWithLua( ent->d_name ) )
				fileEntries.push_back( ent->d_name );
		}
		closedir( dir );
	}
#endif

	std::sort( dirEntries.begin(), dirEntries.end() );
	for( size_t i = 0; i < dirEntries.size(); ++i )
	{
		char szRel[512];
		snprintf( szRel, sizeof( szRel ), "entities/%s/init.lua", dirEntries[i].c_str() );
		XGLua_LoadOneEntityFile( L, szRel, dirEntries[i].c_str() );
	}

	std::sort( fileEntries.begin(), fileEntries.end() );
	for( size_t i = 0; i < fileEntries.size(); ++i )
	{
		const std::string &fn = fileEntries[i];
		char szClass[64];
		size_t copyLen = fn.size() - 4;
		if( copyLen >= sizeof( szClass ) )
			copyLen = sizeof( szClass ) - 1;
		memcpy( szClass, fn.c_str(), copyLen );
		szClass[copyLen] = '\0';

		char szRel[512];
		snprintf( szRel, sizeof( szRel ), "entities/%s", fn.c_str() );
		XGLua_LoadOneEntityFile( L, szRel, szClass );
	}
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
	const int n = XGLua_IncludeImpl( L, XGLua_ReadEngineScript, g_szGameDir );
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
// lua_openscript console command
//===========================================================================

// Run one script from the mod's lua/ tree. The path is validated by the
// production include rules (XGLua_IncludePath inside XGLua_LoadScriptChecked):
// relative to lua/, no absolute paths, no drive letters, no traversal out
// of the tree, length- and character-bounded. Execution goes through the
// protected XGLua_ExecuteScript with the standard error reporter, so
// missing / unreadable files, syntax errors and runtime errors all leave
// the stack restored and name the script. Repeated runs and failures leave
// the VM usable. No permission gating exists - like lua_run this is a
// trusted-server command.
static void XGLua_SVEmitLine( const char *line )
{
	ALERT( at_console, "%s\n", line );
}

static void XGLua_Mem_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	char buf[128];
	if( XGLua_DevMemStats( g_xglua_L, buf, sizeof( buf ) ) )
		ALERT( at_console, "[XGLua] server VM memory: %s\n", buf );
}

static void XGLua_DumpHooks_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	ALERT( at_console, "[XGLua] server hooks:\n" );
	XGLua_DevDumpHooks( g_xglua_L, XGLua_SVEmitLine, XGLua_ReportError, "XGLua" );
}

static void XGLua_Refresh_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	if( CMD_ARGC() < 2 )
	{
		ALERT( at_console, "usage: xgl_refresh <path relative to lua/>\n" );
		return;
	}
	const char *path = CMD_ARGV( 1 );
	if( !XGLua_LoadScriptChecked( g_xglua_L, path ) )
	{
		ALERT( at_console, "[XGLua] xgl_refresh: failed: %s\n", path );
		return;
	}
	XGLua_DevTriggerReloaded( g_xglua_L, XGLua_ReportError );
	ALERT( at_console, "[XGLua] xgl_refresh: reloaded %s\n", path );
}

static void XGLua_Reload_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	XGLua_LoadFolder( g_xglua_L, "autorun", false );
	XGLua_LoadFolder( g_xglua_L, "autorun/server", true );
	XGLua_DevTriggerReloaded( g_xglua_L, XGLua_ReportError );
	ALERT( at_console, "[XGLua] xgl_reload: autorun scripts reloaded\n" );
}

static void XGLua_LuaOpenScript_f( void )
{
	if( !g_xglua_L )
	{
		ALERT( at_console, "[XGLua] no Lua VM running\n" );
		return;
	}
	if( CMD_ARGC() < 2 )
	{
		ALERT( at_console, "usage: lua_openscript <path relative to lua/>\n" );
		return;
	}

	const char *path = CMD_ARGV( 1 );
	if( !XGLua_LoadScriptChecked( g_xglua_L, path ) )
	{
		ALERT( at_console, "[XGLua] lua_openscript: failed: %s\n", path );
		return;
	}
	ALERT( at_console, "[XGLua] lua_openscript: ran %s\n", path );
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
// empty selects the default for the active rules; "none" disables Lua gamemodes
static cvar_t xgl_gamemode = { "xgl_gamemode", "" };

void XGLua_Init( void )
{
	g_engfuncs.pfnAddServerCommand( "lua_run", XGLua_LuaRun_f );
	g_engfuncs.pfnAddServerCommand( "lua_openscript", XGLua_LuaOpenScript_f );
	g_engfuncs.pfnAddServerCommand( "xgl_mem", XGLua_Mem_f );
	g_engfuncs.pfnAddServerCommand( "xgl_dump_hooks", XGLua_DumpHooks_f );
	g_engfuncs.pfnAddServerCommand( "xgl_refresh", XGLua_Refresh_f );
	g_engfuncs.pfnAddServerCommand( "xgl_reload", XGLua_Reload_f );
	g_engfuncs.pfnAddServerCommand( "lua_cmd", XGLua_LuaCmd_f );
	CVAR_REGISTER( &xgl_gamemode );
	XGLua_NetInit();
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
	bool exePathReady = false;
#ifdef _WIN32
	DWORD exeLen = GetModuleFileNameA( NULL, szExe, sizeof( szExe ) );
	exePathReady = exeLen > 0 && exeLen < sizeof( szExe );
#elif defined( __APPLE__ )
	uint32_t exeSize = sizeof( szExe );
	exePathReady = _NSGetExecutablePath( szExe, &exeSize ) == 0;
#elif defined( __linux__ )
	ssize_t exeLen = readlink( "/proc/self/exe", szExe, sizeof( szExe ) - 1 );
	if( exeLen > 0 && (size_t)exeLen < sizeof( szExe ) - 1 )
	{
		szExe[exeLen] = '\0';
		exePathReady = true;
	}
#endif
	if( exePathReady )
	{
		char *pLast = strrchr( szExe, '\\' );
		if( !pLast )
			pLast = strrchr( szExe, '/' );
		if( pLast )
		{
			*pLast = '\0';
			szExeDir = szExe;
		}
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

	if( !szChosen && g_engfuncs.pfnLoadFileForMe && g_engfuncs.pfnFreeFile )
	{
		// The bootstrap can live entirely in an engine-mounted archive. The
		// reported directory is still used for disk-only automatic scanning.
		char *probe = NULL;
		size_t probeLen = 0;
		if( XGLua_ReadEngineScript( g_szGameDir, "includes/init.lua", &probe, &probeLen ) == XGLUA_READ_OK )
		{
			free( probe );
			szChosen = g_szGameDir;
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
	XGLua_RegisterConsole( L );
	XGLua_RegisterNetLibs( L );
	XGLua_RegisterNPCLibs( L );

	// 1. core Lua layer (init.lua loads modules + extensions + enum)
	if( !XGLua_RunCoreInit( &g_xglua_L, L, XGLua_ReadEngineScript, g_szGameDir, XGLua_ReportError ) )
	{
		ALERT( at_console, "[XGLua] core init failed, VM not started\n" );
		return;
	}

	// Resolve the native rules first: gameplay mode and engine session are
	// separate decisions. Legacy mode manifests default to multiplayer.
	{
		const char *nativeRules = "singleplayer";
		if( g_pGameRules && g_pGameRules->IsTeamplay() ) nativeRules = "teamplay";
		else if( g_pGameRules && g_pGameRules->IsBustingGame() ) nativeRules = "busters";
		else if( g_pGameRules && g_pGameRules->IsMultiplayer() ) nativeRules = "multiplayer";
		const char *family = gpGlobals->coop ? "coop" : nativeRules;

		const char *configured = CVAR_GET_STRING( "xgl_gamemode" );
		const char *szMode = configured ? configured : "";
		if( !szMode[0] )
		{
			if( std::strcmp( family, "singleplayer" ) == 0 ) szMode = "campaign";
			else if( std::strcmp( family, "multiplayer" ) == 0 ) szMode = "deathmatch";
		}
		if( std::strcmp( szMode, "none" ) == 0 ) szMode = "";

		// Snapshot for Lua: it describes the active native rules, not a way
		// for scripts to change the engine session or g_pGameRules.
		lua_newtable( L );
		lua_pushstring( L, nativeRules ); lua_setfield( L, -2, "nativeRules" );
		lua_pushstring( L, family ); lua_setfield( L, -2, "compatibility" );
		lua_pushboolean( L, gpGlobals->deathmatch != 0 ); lua_setfield( L, -2, "deathmatch" );
		lua_pushboolean( L, gpGlobals->coop != 0 ); lua_setfield( L, -2, "coop" );
		lua_pushinteger( L, gpGlobals->maxClients ); lua_setfield( L, -2, "maxClients" );
		lua_pushstring( L, szMode ); lua_setfield( L, -2, "requestedMode" );
		lua_setglobal( L, "XGLUA_MODE_CONTEXT" );

		if( szMode[0] )
		{
			char szGMError[256];
			XGLua_GMResult gm = XGLua_LoadGamemodeChain( L, XGLua_ReadEngineRoot, g_szGameDir,
				szMode, szGMError, sizeof( szGMError ), family );
			if( gm == XGLUA_GM_OK )
				ALERT( at_console, "[XGLua] gamemode loaded: %s (rules: %s)\n", szMode, family );
			else
				ALERT( at_console, "[XGLua] gamemode '%s' not loaded (%d): %s\n", szMode, (int)gm, szGMError );
		}
	}

	// 3. autorun (top level, then server/ recursively)
	XGLua_LoadFolder( L, "autorun", false );
	XGLua_LoadFolder( L, "autorun/server", true );

	// 4. weapons — each file builds the global SWEP table
	XGLua_LoadFolder( L, "weapons", false, "SWEP" );

	// Addons load before the cache freezes; each script has its own protected call.
	XGLua_LoadAddonWeapons( L, g_szGameDir, XGLua_ReadEngineRoot, g_szGameDir, XGLua_ReportError );

	// 4b. snapshot SWEP tables into the C++ cache (ItemInfoArray etc.)
	XGLua_BuildWeaponCache( L );

	// 5. scripted entities — single files and <name>/init.lua folders
	XGLua_LoadEntitiesFolder( L );
	XGLua_BuildEntityCache( L );
	XGLua_LoadFolder( L, "npcs", false, "NPC" );
	XGLua_BuildNPCCache( L );

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
	XGLua_ClearEntityCache();
	XGLua_ClearNPCCache();
	XGLua_NetShutdown();
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
	XGLua_NetFrame( L );
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

void XGLua_PlayerInitialSpawn( CBasePlayer *pPlayer )
{
	// hook.Run( "PlayerInitialSpawn", ply ): fired from ClientPutInServer
	// once per connection, before the first Spawn (GMod ordering). Distinct
	// from PlayerSpawn, which fires on every spawn/respawn. The player object
	// is constructed and its userinfo prefs are read at this point.
	XGLua_HookRunProtected( g_xglua_L, "PlayerInitialSpawn", XGLua_PushSpawnedPlayer, pPlayer, XGLua_ReportError );
}

BOOL XGLua_PlayerLoadout( CBasePlayer *pPlayer )
{
	// hook.Run( "PlayerLoadout", ply ): strict-boolean control over the vanilla
	// default spawn loadout. Only a genuine Lua true from the hook chain (a
	// plain hook return or the GM:PlayerLoadout fallback) skips the hardcoded
	// crowbar/9mm/ammo in CHalfLifeMultiplay::PlayerSpawn; no VM, no hook, no
	// method, any other return type, or a reported Lua error keeps the vanilla
	// behavior. Never interrupts the spawn itself.
	if( !g_xglua_L || !pPlayer )
		return FALSE;
	return XGLua_HookRunBool( g_xglua_L, "PlayerLoadout", XGLua_PushSpawnedPlayer, pPlayer,
		XGLua_ReportError ) ? TRUE : FALSE;
}

// hook.Run( "PlayerDeath", victim, inflictor, attacker ): notification-only
// dispatch from CBasePlayer::Killed (placed before the gib/normal split so
// every death fires exactly once). The inflictor comes from
// g_pevLastInflictor and the attacker from the Killed parameter; invalid or
// empty entities map to nil instead of pushing a handle for a dead edict.
// Callback errors are reported and contained - the death flow itself is
// never interrupted.
struct XGLua_DeathArgs { CBasePlayer *victim; entvars_t *inflictor; entvars_t *attacker; };

static void XGLua_PushEntOrNull( lua_State *L, entvars_t *pev )
{
	if( !FNullEnt( pev ) )
		XGLua_PushEntity( L, ENT( pev ) );
	else
		lua_pushnil( L );
}

static int XGLua_PushDeathArgs( lua_State *L, void *ctx )
{
	XGLua_DeathArgs *a = (XGLua_DeathArgs *)ctx;
	XGLua_PushEntity( L, a->victim->edict() );	// victim: the dying player
	XGLua_PushEntOrNull( L, a->inflictor );
	XGLua_PushEntOrNull( L, a->attacker );
	return 3;
}

void XGLua_PlayerDeath( CBasePlayer *victim, entvars_t *inflictor, entvars_t *attacker )
{
	if( !g_xglua_L || !victim )
		return;
	XGLua_DeathArgs args = { victim, inflictor, attacker };
	XGLua_HookRunProtected( g_xglua_L, "PlayerDeath", XGLua_PushDeathArgs, &args, XGLua_ReportError );
}

void XGLua_PlayerDisconnected( CBasePlayer *pPlayer )
{
	// hook.Run( "PlayerDisconnected", ply ): notification-only, dispatched
	// from ClientDisconnect before the edict is neutralized. Errors are
	// reported and contained - they cannot block the native cleanup.
	if( !g_xglua_L || !pPlayer )
		return;
	XGLua_HookRunProtected( g_xglua_L, "PlayerDisconnected", XGLua_PushSpawnedPlayer, pPlayer, XGLua_ReportError );
}

// hook.Run( "PlayerSay", ply, text, teamOnly ): text-rewrite hook for the
// say / say_team client commands. Host_Say passes text only after its native
// rate, empty-message and Unicode checks. A non-empty string replaces the
// chat text, an empty string swallows it, and anything else (nil, non-string,
// Lua error) passes the original text through untouched.
struct XGLua_SayArgs { CBasePlayer *player; const char *text; int teamonly; };

static int XGLua_PushSayArgs( lua_State *L, void *ctx )
{
	XGLua_SayArgs *a = (XGLua_SayArgs *)ctx;
	XGLua_PushEntity( L, a->player->edict() );
	lua_pushstring( L, a->text );
	lua_pushboolean( L, a->teamonly );
	return 3;
}

XGLuaSayResult XGLua_PlayerSay( CBasePlayer *pPlayer, const char *text, int teamonly, char *out, size_t outlen )
{
	if( !g_xglua_L || !pPlayer || !text || !out || !outlen )
		return XGLUA_SAY_PASSTHROUGH;

	XGLua_SayArgs args = { pPlayer, text, teamonly != 0 };
	char szResult[256];
	if( !XGLua_HookRunString( g_xglua_L, "PlayerSay", XGLua_PushSayArgs, &args, szResult, sizeof( szResult ), XGLua_ReportError ) )
		return XGLUA_SAY_PASSTHROUGH;
	if( !szResult[0] )
		return XGLUA_SAY_SWALLOWED;
	snprintf( out, outlen, "%s", szResult );
	return XGLUA_SAY_REPLACED;
}

// Pickup vetoes (PlayerCanPickupWeapon / PlayerCanPickupItem): deny-style
// decisions over the shared protected hook core - only a strict Lua false
// from the hook chain returns FALSE; true, nil, non-boolean results,
// missing handlers and reported Lua errors all keep the native allow
// behavior. Hook priority and the GAMEMODE fallback stay inside hook.Run;
// hook-internal errors are contained by hook.lua and the chain continues.
struct XGLua_PickupArgs { CBasePlayer *player; CBaseEntity *item; };

static int XGLua_PushPickupArgs( lua_State *L, void *ctx )
{
	XGLua_PickupArgs *a = (XGLua_PickupArgs *)ctx;
	XGLua_PushEntity( L, a->player->edict() );
	XGLua_PushEntity( L, a->item->edict() );
	return 2;
}

static BOOL XGLua_PickupVeto( const char *event, CBasePlayer *pPlayer, CBaseEntity *pItem )
{
	if( !g_xglua_L || !pPlayer || !pItem )
		return FALSE;
	XGLua_PickupArgs args = { pPlayer, pItem };
	return XGLua_HookDenyBool( g_xglua_L, event, XGLua_PushPickupArgs, &args, XGLua_ReportError ) ? FALSE : TRUE;
}

BOOL XGLua_PlayerCanPickupWeapon( CBasePlayer *pPlayer, CBaseEntity *pWeapon )
{
	return XGLua_PickupVeto( "PlayerCanPickupWeapon", pPlayer, pWeapon );
}

BOOL XGLua_PlayerCanPickupItem( CBasePlayer *pPlayer, CBaseEntity *pItem )
{
	return XGLua_PickupVeto( "PlayerCanPickupItem", pPlayer, pItem );
}
