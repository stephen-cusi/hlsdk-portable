/***
*
*	XGLua — Client-side Lua VM lifecycle (M3 Phase 3B)
*
*	Realm: CLIENT. Recreated on HUD_VidInit (map load / connect) so stale
*	state never leaks across servers or maps.
*
****/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <utility>

#include "hud.h"
#include "cl_util.h"
#include "lua/xglua_dev.h"
#include "lua/xglua_cl.h"
#include "lua/xglua_safe.h"
#include "lua/xglua_hook.h"
// engine's cdll_int.h typedefs HSPRITE as int; the Windows SDK (minwindef.h)
// redefines it as struct HSPRITE__ * - rename it away for this TU only.
#define HSPRITE XGLUA_WIN_HSPRITE
#include <windows.h> // FindFirstFileA for autorun scan
#undef HSPRITE
#include "lua/xglua_vm_timer.h"
extern "C" {
#include "lualib.h"
#include "lauxlib.h"
}

lua_State *g_xglua_cl_L = NULL;

//===========================================================================
// Error reporting & VFS script loading
//===========================================================================

void XGLua_CL_ReportError( lua_State *L, const char *context )
{
	char buf[512];
	snprintf( buf, sizeof( buf ), "[XGLua CL] %s error:\n%s\n", context, XGLua_ErrorText( L, -1 ) );
	ConsolePrint( buf );
	lua_pop( L, 1 );
}

static std::vector<std::pair<std::string, std::string> > g_CLMemVFS;

void XGLua_CL_MemVFSStore( const char *relpath, const char *data, size_t len )
{
	if( !relpath || !relpath[0] ) return;
	std::string key = relpath;
	for( size_t i = 0; i < key.size(); ++i )
		if( key[i] == '\\' ) key[i] = '/';
	for( size_t i = 0; i < g_CLMemVFS.size(); ++i )
	{
		if( g_CLMemVFS[i].first == key )
		{
			g_CLMemVFS[i].second.assign( data ? data : "", len );
			return;
		}
	}
	g_CLMemVFS.push_back( std::make_pair( key, std::string( data ? data : "", len ) ) );
}

bool XGLua_CL_MemVFSFind( const char *relpath, std::string &out )
{
	if( !relpath || !relpath[0] ) return false;
	std::string key = relpath;
	for( size_t i = 0; i < key.size(); ++i )
		if( key[i] == '\\' ) key[i] = '/';
	for( size_t i = 0; i < g_CLMemVFS.size(); ++i )
	{
		if( g_CLMemVFS[i].first == key )
		{
			out = g_CLMemVFS[i].second;
			return true;
		}
	}
	return false;
}

void XGLua_CL_MemVFSClear( void )
{
	g_CLMemVFS.clear();
}

static bool CL_LoadFileViaEngine( const char *relpath, std::string &out )
{
	if( !relpath || !relpath[0] || !gEngfuncs.COM_LoadFile || !gEngfuncs.COM_FreeFile )
		return false;

	// Reject traversal / absolute paths
	if( relpath[0] == '/' || relpath[0] == '\\' || std::strstr( relpath, ".." ) != NULL )
		return false;

	if( XGLua_CL_MemVFSFind( relpath, out ) )
		return true;

	char full[512];
	snprintf( full, sizeof( full ), "lua/%s", relpath );
	int length = 0;
	byte *data = gEngfuncs.COM_LoadFile( full, 5, &length );
	if( !data || length < 0 )
		return false;

	out.assign( (const char *)data, (size_t)length );
	gEngfuncs.COM_FreeFile( data );
	return true;
}

static bool CL_RunScriptFile( lua_State *L, const char *relpath )
{
	std::string src;
	if( !CL_LoadFileViaEngine( relpath, src ) )
		return false;

	char chunk[256];
	snprintf( chunk, sizeof( chunk ), "@lua/%s", relpath );
	if( luaL_loadbuffer( L, src.c_str(), src.size(), chunk ) != 0 )
	{
		XGLua_CL_ReportError( L, chunk );
		return false;
	}
	if( lua_pcall( L, 0, 0, 0 ) != 0 )
	{
		XGLua_CL_ReportError( L, chunk );
		return false;
	}
	return true;
}

//===========================================================================
// Client autorun scan (lua/autorun and lua/autorun/client)

static void CL_LoadAutorunFolder( lua_State *L, const char *relfolder )
{
#ifdef _WIN32
	const char *gamedir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : NULL;
	if( !gamedir || !gamedir[0] ) return;
	char mask[512];
	snprintf( mask, sizeof( mask ), "%s/lua/%s/*.lua", gamedir, relfolder );
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( mask, &fd );
	if( h == INVALID_HANDLE_VALUE ) return;
	do
	{
		if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) continue;
		char rel[512];
		snprintf( rel, sizeof( rel ), "%s/%s", relfolder, fd.cFileName );
		CL_RunScriptFile( L, rel );
	} while( FindNextFileA( h, &fd ) );
	FindClose( h );
#else
	(void)L; (void)relfolder;
#endif
}

//


// Base client globals
//===========================================================================

static void XGLua_CLEmitLine( const char *line )
{
	char buf[600];
	snprintf( buf, sizeof( buf ), "%s\n", line );
	ConsolePrint( buf );
}

static void XGLua_CL_Mem_f( void )
{
	if( !g_xglua_cl_L ) { ConsolePrint( "[XGLua CL] no client Lua VM running\n" ); return; }
	char stats[128], buf[192];
	if( XGLua_DevMemStats( g_xglua_cl_L, stats, sizeof( stats ) ) )
	{
		snprintf( buf, sizeof( buf ), "[XGLua CL] client VM memory: %s\n", stats );
		ConsolePrint( buf );
	}
}

static void XGLua_CL_DumpHooks_f( void )
{
	if( !g_xglua_cl_L ) { ConsolePrint( "[XGLua CL] no client Lua VM running\n" ); return; }
	ConsolePrint( "[XGLua CL] client hooks:\n" );
	XGLua_DevDumpHooks( g_xglua_cl_L, XGLua_CLEmitLine, XGLua_CL_ReportError, "XGLua CL" );
}

static void XGLua_CL_Refresh_f( void )
{
	if( !g_xglua_cl_L ) { ConsolePrint( "[XGLua CL] no client Lua VM running\n" ); return; }
	if( gEngfuncs.Cmd_Argc() < 2 )
	{
		ConsolePrint( "usage: xgl_refresh_cl <path relative to lua/>\n" );
		return;
	}
	const char *path = gEngfuncs.Cmd_Argv( 1 );
	if( !CL_RunScriptFile( g_xglua_cl_L, path ) )
	{
		ConsolePrint( "[XGLua CL] xgl_refresh_cl: failed\n" );
		return;
	}
	XGLua_DevTriggerReloaded( g_xglua_cl_L, XGLua_CL_ReportError );
	ConsolePrint( "[XGLua CL] xgl_refresh_cl: reloaded\n" );
}

static void XGLua_CL_Reload_f( void )
{
	if( !g_xglua_cl_L ) { ConsolePrint( "[XGLua CL] no client Lua VM running\n" ); return; }
	CL_LoadAutorunFolder( g_xglua_cl_L, "autorun" );
	CL_LoadAutorunFolder( g_xglua_cl_L, "autorun/client" );
	XGLua_DevTriggerReloaded( g_xglua_cl_L, XGLua_CL_ReportError );
	ConsolePrint( "[XGLua CL] xgl_reload_cl: autorun scripts reloaded\n" );
}

static int CL_print( lua_State *L )
{
	int n = lua_gettop( L );
	std::string out = "[Lua CL] ";
	for( int i = 1; i <= n; ++i )
	{
		size_t len = 0;
		const char *s = lua_tolstring( L, i, &len );
		if( s )
			out.append( s, len );
		else
			out += lua_typename( L, lua_type( L, i ) );
		if( i < n )
			out += "  ";
	}
	out += "\n";
	ConsolePrint( out.c_str() );
	return 0;
}

static int CL_CurTime( lua_State *L )
{
	lua_pushnumber( L, gEngfuncs.GetClientTime ? (double)gEngfuncs.GetClientTime() : (double)gHUD.m_flTime );
	return 1;
}

static int CL_RealTime( lua_State *L )
{
	lua_pushnumber( L, (double)clock() / CLOCKS_PER_SEC );
	return 1;
}

static int CL_FrameTime( lua_State *L )
{
	lua_pushnumber( L, gHUD.m_flTimeDelta );
	return 1;
}

static int CL_ScrW( lua_State *L )
{
	lua_pushinteger( L, ScreenWidth );
	return 1;
}

static int CL_ScrH( lua_State *L )
{
	lua_pushinteger( L, ScreenHeight );
	return 1;
}

static int CL_Color( lua_State *L )
{
	int r = (int)luaL_optnumber( L, 1, 255 );
	int g = (int)luaL_optnumber( L, 2, 255 );
	int b = (int)luaL_optnumber( L, 3, 255 );
	int a = (int)luaL_optnumber( L, 4, 255 );
	lua_createtable( L, 0, 4 );
	lua_pushinteger( L, r ); lua_setfield( L, -2, "r" );
	lua_pushinteger( L, g ); lua_setfield( L, -2, "g" );
	lua_pushinteger( L, b ); lua_setfield( L, -2, "b" );
	lua_pushinteger( L, a ); lua_setfield( L, -2, "a" );
	return 1;
}

static int CL_AddCSLuaFile( lua_State *L )
{
	( void )L;
	return 0;
}

static int CL_include( lua_State *L )
{
	const char *rel = luaL_checkstring( L, 1 );
	std::string src;
	if( !CL_LoadFileViaEngine( rel, src ) )
		return luaL_error( L, "include: cannot open 'lua/%s'", rel );

	char chunk[256];
	snprintf( chunk, sizeof( chunk ), "@lua/%s", rel );
	if( luaL_loadbuffer( L, src.c_str(), src.size(), chunk ) != 0 )
		return lua_error( L );
	lua_call( L, 0, LUA_MULTRET );
	return lua_gettop( L ) - 1;
}

//===========================================================================
// lua_run_cl console command
//===========================================================================

static void XGLua_CL_LuaRun_f( void )
{
	if( !g_xglua_cl_L )
	{
		ConsolePrint( "[XGLua CL] no client Lua VM running\n" );
		return;
	}
	std::string code;
	int argc = gEngfuncs.Cmd_Argc();
	for( int i = 1; i < argc; ++i )
	{
		if( i > 1 ) code += " ";
		code += gEngfuncs.Cmd_Argv( i );
	}
	if( code.empty() )
	{
		ConsolePrint( "usage: lua_run_cl <code>\n" );
		return;
	}

	lua_State *L = g_xglua_cl_L;
	if( luaL_loadbuffer( L, code.c_str(), code.size(), "lua_run_cl" ) != 0 )
	{
		XGLua_CL_ReportError( L, "lua_run_cl load" );
		return;
	}
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		XGLua_CL_ReportError( L, "lua_run_cl" );
		return;
	}
	if( lua_type( L, -1 ) == LUA_TSTRING )
	{
		char buf[512];
		snprintf( buf, sizeof( buf ), "[Lua CL] %s\n", lua_tostring( L, -1 ) );
		ConsolePrint( buf );
	}
	else if( lua_type( L, -1 ) == LUA_TNUMBER )
	{
		char buf[64];
		snprintf( buf, sizeof( buf ), "[Lua CL] %g\n", lua_tonumber( L, -1 ) );
		ConsolePrint( buf );
	}
	lua_pop( L, 1 );
}

//===========================================================================
// Lifecycle
//===========================================================================

void XGLua_CL_Init( void )
{
	gEngfuncs.pfnAddCommand( "lua_run_cl", XGLua_CL_LuaRun_f );
	gEngfuncs.pfnAddCommand( "xgl_mem_cl", XGLua_CL_Mem_f );
	gEngfuncs.pfnAddCommand( "xgl_dump_hooks_cl", XGLua_CL_DumpHooks_f );
	gEngfuncs.pfnAddCommand( "xgl_refresh_cl", XGLua_CL_Refresh_f );
	gEngfuncs.pfnAddCommand( "xgl_reload_cl", XGLua_CL_Reload_f );
	XGLua_CL_NetHookMessages();
}

void XGLua_CL_Shutdown( void )
{
	XGLua_CL_NetReset();
	if( !g_xglua_cl_L )
		return;
	XGLua_HookRunProtected( g_xglua_cl_L, "ShutDown", NULL, NULL, XGLua_CL_ReportError );
	lua_close( g_xglua_cl_L );
	g_xglua_cl_L = NULL;
}

void XGLua_CL_Startup( void )
{
	// Recreate a clean client VM on each map/video init.
	XGLua_CL_Shutdown();

	lua_State *L = luaL_newstate();
	if( !L )
	{
		ConsolePrint( "[XGLua CL] FAILED to create client Lua state\n" );
		return;
	}
	g_xglua_cl_L = L;
	luaL_openlibs( L );

	lua_pushboolean( L, 0 ); lua_setglobal( L, "SERVER" );
	lua_pushboolean( L, 1 ); lua_setglobal( L, "CLIENT" );

	static const luaL_Reg baseglobals[] =
	{
		{ "print",     CL_print },
		{ "CurTime",   CL_CurTime },
		{ "RealTime",  CL_RealTime },
		{ "FrameTime", CL_FrameTime },
		{ "ScrW",      CL_ScrW },
		{ "ScrH",      CL_ScrH },
		{ "Color",     CL_Color },
		{ "include",   CL_include },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = baseglobals; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setglobal( L, f->name );
	}

	XGLua_CL_RegisterSurface( L );
	XGLua_CL_RegisterNet( L );

	if( !CL_RunScriptFile( L, "includes/init.lua" ) )
	{
		ConsolePrint( "[XGLua CL] failed to load lua/includes/init.lua\n" );
		XGLua_CL_Shutdown();
		return;
	}

	CL_LoadAutorunFolder( L, "autorun" );
	CL_LoadAutorunFolder( L, "autorun/client" );

	XGLua_HookRunProtected( L, "Initialize", NULL, NULL, XGLua_CL_ReportError );
	ConsolePrint( "[XGLua CL] client VM ready\n" );
}

void XGLua_CL_Frame( double time )
{
	(void)time;
	if( !g_xglua_cl_L )
		return;
	XGLua_RunTimerTick( g_xglua_cl_L, XGLua_CL_ReportError );
	XGLua_HookRunProtected( g_xglua_cl_L, "Think", NULL, NULL, XGLua_CL_ReportError );
}

void XGLua_CL_HUDPaint( void )
{
	if( !g_xglua_cl_L )
		return;
	XGLua_HookRunProtected( g_xglua_cl_L, "HUDPaint", NULL, NULL, XGLua_CL_ReportError );
}
