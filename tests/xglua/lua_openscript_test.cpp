// Regression for the lua_openscript server command (dlls/lua/xglua_vm.cpp).
//
// HELPER-LEVEL TEST, clearly scoped: OpenScriptMirror reproduces the
// XGLua_LoadScriptChecked wrapper's path-check / execute / report / stack-
// restore flow. It directly calls these REAL production helpers on a real
// temporary directory tree:
//   - XGLua_IncludePath (path validation used by XGLua_LoadScriptChecked):
//     absolute paths, drive letters, traversal, backslashes, empty and
//     oversized inputs, embedded NULs
//   - XGLua_ExecuteScript + XGLua_ReadDiskScript (execution and file reader
//     used by XGLua_LoadScriptChecked): success, repeat, missing file, syntax
//     error, runtime error, and a good script after a failed one
// The command callback itself (CMD_ARGC/CMD_ARGV, ALERT output, and
// registration) is reviewed statically; this is not a command-integration
// test.
// Every path asserts the Lua stack top and the sentinel survive.
//
// Build & run (ARM64, native):
//   /MT removes the VCRUNTIME140.dll dependency; lua_shared.dll must sit
//   NEXT TO the exe. Without /MT, ship VCRUNTIME140.dll alongside too.
//
//   cl /nologo /W3 /EHsc /std:c++17 /utf-8 /MT /I<luajit>/src ^
//      /I<repo>/dlls/lua lua_openscript_test.cpp /link ^
//      /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
//   copy <luajit>\dist\arm64\lua_shared.dll .
//   lua_openscript_test.exe

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "xglua_vm_include.h"

//===========================================================================
// harness
//===========================================================================

static int fail( const char *what )
{
	std::printf( "FAIL: %s\n", what );
	std::fflush( stdout );
	return 1;
}

static DWORD WINAPI Watchdog( LPVOID )
{
	Sleep( 120000 );
	std::printf( "WATCHDOG: timeout after 120s\n" );
	std::fflush( stdout );
	_exit( 2 );
	return 0;
}

static bool sentinelOk( lua_State *L, int baseline )
{
	return lua_gettop( L ) == baseline && lua_isstring( L, 1 ) &&
		std::strcmp( lua_tostring( L, 1 ), "sentinel" ) == 0;
}

// Mirror of XGLua_LoadScriptChecked's body (vm.cpp) - the exact code the
// command calls. Returns false after printing, like the production helper;
// on Lua failures the error message is captured into errbuf first.
static bool OpenScriptMirror( lua_State *L, const char *gamedir, const char *relpath, char *err, size_t errlen )
{
	err[0] = '\0';
	const int top = lua_gettop( L );
	char relative[XGLUA_INCLUDE_PATH_MAX];
	if( !relpath || !XGLua_IncludePath( NULL, relpath, std::strlen( relpath ), relative, sizeof( relative ) ) )
	{
		snprintf( err, errlen, "[XGLua] invalid script path" );
		return false;
	}
	int status = XGLua_ExecuteScript( L, relative, XGLua_ReadDiskScript, (void *)gamedir, 0 );
	if( status != (int)XGLUA_READ_OK )
	{
		const char *message = lua_tostring( L, -1 );
		snprintf( err, errlen, "%s", message ? message : "?" );
		lua_settop( L, top );
		return false;
	}
	lua_settop( L, top );
	return true;
}

// temporary mod tree: <tmp>/lua/{ok.lua, sub/ok.lua, broken_syntax.lua, broken_runtime.lua}
static char g_szModDir[MAX_PATH];

static bool write_file( const char *dir, const char *name, const char *content )
{
	char path[MAX_PATH];
	snprintf( path, sizeof( path ), "%s\\%s", dir, name );
	FILE *fp = fopen( path, "wb" );
	if( !fp ) return false;
	fwrite( content, 1, strlen( content ), fp );
	fclose( fp );
	return true;
}

static bool buildModTree()
{
	char tmp[MAX_PATH];
	if( !GetTempPathA( sizeof( tmp ), tmp ) ) return false;
	snprintf( g_szModDir, sizeof( g_szModDir ), "%sxglua_openscript_test", tmp );
	CreateDirectoryA( g_szModDir, NULL );
	char luaDir[MAX_PATH], subDir[MAX_PATH];
	snprintf( luaDir, sizeof( luaDir ), "%s\\lua", g_szModDir );
	snprintf( subDir, sizeof( subDir ), "%s\\sub", luaDir );
	CreateDirectoryA( luaDir, NULL );
	CreateDirectoryA( subDir, NULL );
	if( !write_file( luaDir, "ok.lua", "Loaded = ( Loaded or 0 ) + 1\n" ) ) return false;
	if( !write_file( subDir, "ok.lua", "SubLoaded = ( SubLoaded or 0 ) + 1\n" ) ) return false;
	if( !write_file( luaDir, "broken_syntax.lua", "if true then\n" ) ) return false;
	if( !write_file( luaDir, "broken_runtime.lua", "error( 'expected openscript runtime error' )\n" ) ) return false;
	return true;
}

#define CHECK( cond, msg ) do { if( !( cond ) ) return fail( msg ); } while( 0 )

int main()
{
	std::printf( "SMOKE: harness alive\n" );
	std::fflush( stdout );
	CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );

	if( !buildModTree() )
		return fail( "temp mod tree" );
	std::printf( "mod tree: %s\n", g_szModDir );

	lua_State *L = luaL_newstate();
	if( !L ) return fail( "new state" );
	luaL_openlibs( L );
	lua_pushliteral( L, "sentinel" );

	const int baseline = lua_gettop( L );
	char err[512];

	//-----------------------------------------------------------------------
	// 1. valid script: runs, side effect, stack restored
	//-----------------------------------------------------------------------
	std::printf( "PHASE ok\n" ); std::fflush( stdout );
	{
		CHECK( OpenScriptMirror( L, g_szModDir, "ok.lua", err, sizeof( err ) ), err );
		lua_getglobal( L, "Loaded" );
		CHECK( lua_tointeger( L, -1 ) == 1, "script ran once" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "ok stack" );
	}

	//-----------------------------------------------------------------------
	// 2. repeat execution keeps the VM healthy
	//-----------------------------------------------------------------------
	std::printf( "PHASE repeat\n" ); std::fflush( stdout );
	{
		for( int i = 0; i < 10; ++i )
			CHECK( OpenScriptMirror( L, g_szModDir, "ok.lua", err, sizeof( err ) ), "repeat run" );
		lua_getglobal( L, "Loaded" );
		CHECK( lua_tointeger( L, -1 ) == 11, "script ran 11 times total" );
		lua_pop( L, 1 );
		CHECK( OpenScriptMirror( L, g_szModDir, "sub/ok.lua", err, sizeof( err ) ), "subdir path" );
		lua_getglobal( L, "SubLoaded" );
		CHECK( lua_tointeger( L, -1 ) == 1, "subdir script ran" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "repeat stack" );
	}

	//-----------------------------------------------------------------------
	// 3. missing file: status and message name the path, stack restored
	//-----------------------------------------------------------------------
	std::printf( "PHASE missing\n" ); std::fflush( stdout );
	{
		CHECK( !OpenScriptMirror( L, g_szModDir, "missing.lua", err, sizeof( err ) ), "missing must fail" );
		CHECK( std::strstr( err, "missing.lua" ), "missing names the script" );
		CHECK( std::strstr( err, "not found" ), "missing is a not-found error" );
		CHECK( sentinelOk( L, baseline ), "missing stack" );
	}

	//-----------------------------------------------------------------------
	// 4. syntax error: message carries the @lua/<path> chunk name
	//-----------------------------------------------------------------------
	std::printf( "PHASE syntax\n" ); std::fflush( stdout );
	{
		CHECK( !OpenScriptMirror( L, g_szModDir, "broken_syntax.lua", err, sizeof( err ) ), "syntax must fail" );
		CHECK( std::strstr( err, "broken_syntax.lua" ), "syntax error names the script" );
		CHECK( sentinelOk( L, baseline ), "syntax stack" );
	}

	//-----------------------------------------------------------------------
	// 5. runtime error: contained, message preserved, stack restored
	//-----------------------------------------------------------------------
	std::printf( "PHASE runtime\n" ); std::fflush( stdout );
	{
		CHECK( !OpenScriptMirror( L, g_szModDir, "broken_runtime.lua", err, sizeof( err ) ), "runtime must fail" );
		CHECK( std::strstr( err, "expected openscript runtime error" ), "runtime error message" );
		CHECK( std::strstr( err, "broken_runtime.lua" ), "runtime error names the script" );
		CHECK( sentinelOk( L, baseline ), "runtime stack" );
	}

	//-----------------------------------------------------------------------
	// 6. failure leaves the VM usable for the next script
	//-----------------------------------------------------------------------
	std::printf( "PHASE after-failure\n" ); std::fflush( stdout );
	{
		CHECK( !OpenScriptMirror( L, g_szModDir, "missing.lua", err, sizeof( err ) ), "prefailure" );
		CHECK( OpenScriptMirror( L, g_szModDir, "ok.lua", err, sizeof( err ) ), "good script after failure" );
		lua_getglobal( L, "Loaded" );
		CHECK( lua_tointeger( L, -1 ) == 12, "script still runs after failures" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "after-failure stack" );
	}

	//-----------------------------------------------------------------------
	// 7. path validation (the exact XGLua_IncludePath call): all rejects
	//-----------------------------------------------------------------------
	std::printf( "PHASE path-validation\n" ); std::fflush( stdout );
	{
		static const char *bad[] = {
			"",				// empty
			"/abs.lua",			// absolute forward
			"\\abs.lua",			// absolute backslash
			"C:\\abs.lua",			// drive letter
			"C:/abs.lua",			// drive letter forward
			"..",				// pure parent
			"../escape.lua",		// parent escape
			"a/../../escape.lua",		// nested escape
			"a\\..\\..\\escape.lua",	// backslash escape
			"..\\escape.lua",		// parent escape backslash
			"a:b",				// colon in component
			"a/b:c.lua",			// colon deeper in
			"ok.",				// trailing dot (Win32 alias)
			"ok ",				// trailing space
			NULL };
		char out[XGLUA_INCLUDE_PATH_MAX];
		for( int i = 0; bad[i]; ++i )
			CHECK( !XGLua_IncludePath( NULL, bad[i], std::strlen( bad[i] ), out, sizeof( out ) ), bad[i] );
		// embedded NUL with a lying length
		CHECK( !XGLua_IncludePath( NULL, "ok.lua\0evil", 12, out, sizeof( out ) ), "embedded NUL" );
		// oversized input
		char big[1024];
		std::memset( big, 'a', sizeof( big ) - 1 );
		big[sizeof( big ) - 1] = '\0';
		CHECK( !XGLua_IncludePath( NULL, big, std::strlen( big ), out, sizeof( out ) ), "oversized" );
		// control characters
		CHECK( !XGLua_IncludePath( NULL, "ok\t.lua", 7, out, sizeof( out ) ), "control character" );
		CHECK( sentinelOk( L, baseline ), "path-validation stack" );
	}

	//-----------------------------------------------------------------------
	// 8. rejected paths never reach the reader
	//-----------------------------------------------------------------------
	std::printf( "PHASE no-read\n" ); std::fflush( stdout );
	{
		// a traversal that would resolve outside is rejected before any
		// file access; a valid-but-missing name does reach the reader and
		// fails with not-found (covered above)
		CHECK( !OpenScriptMirror( L, g_szModDir, "../outside.lua", err, sizeof( err ) ), "traversal rejected" );
		CHECK( std::strstr( err, "invalid script path" ), "traversal message" );
		CHECK( sentinelOk( L, baseline ), "no-read stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "lua_openscript helper paths OK" );
	std::fflush( stdout );
	return 0;
}
