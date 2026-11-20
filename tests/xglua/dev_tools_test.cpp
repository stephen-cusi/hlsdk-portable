// Regression for common/lua/xglua_dev.h (xgl_mem, xgl_dump_hooks, OnReloaded)
// against real lua/includes/modules/hook.lua.
// Build with /I<repo>/common /I<repo>/dlls/lua and run from tests/xglua.

#include "test_framework.h"
#include <string>
#include <vector>
#include "lua/xglua_dev.h"

static std::vector<std::string> g_lines;
static int g_errors = 0;

static void CaptureLine( const char *line )
{
	g_lines.push_back( line ? line : "" );
}

static void CaptureError( lua_State *L, const char *ctx )
{
	(void)ctx;
	++g_errors;
	lua_pop( L, 1 );
}

static bool SetupDev( lua_State *L )
{
	g_lines.clear();
	g_errors = 0;
	if( luaL_loadfile( L, "../../lua/includes/modules/hook.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: hook.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

TEST(Dev_mem_stats_format)
{
	char buf[128] = { 0 };
	CHECK( XGLua_DevMemStats( L, buf, sizeof( buf ) ), "DevMemStats succeeds" );
	CHECK( std::strstr( buf, "KB (" ) != NULL, "DevMemStats includes KB" );
	CHECK( std::strstr( buf, "bytes)" ) != NULL, "DevMemStats includes bytes" );
	CHECK_EQ_BOOL( XGLua_DevMemStats( NULL, buf, sizeof( buf ) ), false, "NULL state rejected" );
}

TEST(Dev_dump_hooks_output)
{
	XGLua_DevDumpHooks( L, CaptureLine, CaptureError, "XGLua" );
	CHECK_EQ_INT( (int)g_lines.size(), 1, "empty hooks emits only summary line" );
	CHECK( g_lines[0].find( "0 hook(s) across 0 event(s)" ) != std::string::npos, "empty summary count" );

	g_lines.clear();
	LUA( "hook.Add('Think', 'a', function() end)\n"
		"hook.Add('Think', 'b', function() end)\n"
		"hook.Add('HUDPaint', 'hud1', function() end)" );
	XGLua_DevDumpHooks( L, CaptureLine, CaptureError, "XGLua" );
	CHECK_EQ_INT( (int)g_lines.size(), 3, "2 event lines + 1 summary line" );
	CHECK( g_lines.back().find( "3 hook(s) across 2 event(s)" ) != std::string::npos, "summary matches 3 hooks / 2 events" );
	CHECK_EQ_INT( g_errors, 0, "no errors during dump" );

	// Missing hook global must not crash
	g_lines.clear();
	LUA( "hook = nil" );
	XGLua_DevDumpHooks( L, CaptureLine, CaptureError, "XGLua" );
	CHECK( g_lines.back().find( "0 hook(s) across 0 event(s)" ) != std::string::npos, "missing hook global safe" );
}

TEST(Dev_trigger_on_reloaded)
{
	LUA( "ReloadCount = 0\n"
		"hook.Add('OnReloaded', 'cnt', function() ReloadCount = ReloadCount + 1 end)" );
	XGLua_DevTriggerReloaded( L, CaptureError );
	XGLua_DevTriggerReloaded( L, CaptureError );
	CHECK_LUA_INT( "ReloadCount", 2, "OnReloaded fired twice" );
	CHECK_EQ_INT( g_errors, 0, "no errors on clean OnReloaded" );
}

int main()
{
	xtest::Setup() = SetupDev;
	return xtest::RunAll( "dev_tools" );
}
