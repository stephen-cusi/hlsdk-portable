// Regression for hook.Run("PlayerInitialSpawn", ply) wiring in
// ClientPutInServer. Loads the REAL lua/includes/modules/hook.lua from the
// repository and drives the production XGLua_HookRunProtected dispatch with
// the same single-entity pusher shape the VM layer uses
// (XGLua_PushSpawnedPlayer). PlayerInitialSpawn must stay distinct from
// PlayerSpawn: once per connection vs every spawn/respawn.
//
// Verified:
//   - the player argument reaches plain hooks and the GM fallback with self
//   - missing hook and missing GAMEMODE stay silent no-ops
//   - a raising GM fallback is reported and contained; hook-internal errors
//     are contained by hook.lua itself
//   - PlayerInitialSpawn never cross-fires PlayerSpawn hooks or vice versa
//   - stack top and sentinel survive every path
//
// Build & run (ARM64, native): /MT removes the test exe's CRT dependency,
// but lua_shared.dll may still need VCRUNTIME140.dll. Keep both DLLs beside
// the exe or install the matching runtime. Run from tests/xglua so the
// relative hook.lua path below resolves.
//
//   cl /nologo /W3 /EHsc /std:c++17 /utf-8 /MT /I<luajit>/src ^
//      /I<repo>/dlls/lua /Fe:<repo>/tests/xglua/player_initial_spawn_test.exe ^
//      player_initial_spawn_test.cpp /link ^
//      /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
//   copy <luajit>\dist\arm64\lua_shared.dll <repo>\tests\xglua\
//   copy <VS-redist>\arm64\Microsoft.VC143.CRT\vcruntime140.dll <repo>\tests\xglua\ (if not installed)
//   player_initial_spawn_test.exe

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "xglua_hook.h"

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

static int reportedErrors = 0;
static char lastMessage[256] = "";

static void onError( lua_State *L, const char *context )
{
	++reportedErrors;
	const char *message = XGLua_ErrorText( L, -1 );
	snprintf( lastMessage, sizeof( lastMessage ), "%s", message ? message : "" );
	(void)context;
	lua_pop( L, 1 );
}

static int playerRef = 0;
static int PushPlayer( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
	return 1;
}

// The production dispatch behind XGLua_PlayerInitialSpawn.
static void RunInitialSpawn( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerInitialSpawn", PushPlayer, NULL, onError );
}

// And the sibling event, to prove the two never cross-fire.
static void RunSpawn( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerSpawn", PushPlayer, NULL, onError );
}

static bool sentinelOk( lua_State *L, int baseline )
{
	return lua_gettop( L ) == baseline && lua_isstring( L, 1 ) &&
		std::strcmp( lua_tostring( L, 1 ), "sentinel" ) == 0;
}

#define CHECK( cond, msg ) do { if( !( cond ) ) return fail( msg ); } while( 0 )

int main()
{
	std::printf( "SMOKE: harness alive\n" );
	std::fflush( stdout );
	CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );

	lua_State *L = luaL_newstate();
	if( !L ) return fail( "new state" );
	luaL_openlibs( L );
	lua_pushliteral( L, "sentinel" );

	if( luaL_loadfile( L, "../../lua/includes/modules/hook.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "FAIL: hook.lua load: %s\n", lua_tostring( L, -1 ) );
		return 1;
	}

	luaL_dostring( L, "return { tag = 'player' }" );
	playerRef = luaL_ref( L, LUA_REGISTRYINDEX );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. missing hook and missing gamemode: silent no-op
	//-----------------------------------------------------------------------
	std::printf( "PHASE missing\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		RunInitialSpawn( L );
		CHECK( reportedErrors == 0, "no hook, no GM must stay silent" );
		CHECK( sentinelOk( L, baseline ), "missing stack" );
	}

	//-----------------------------------------------------------------------
	// 2. player argument identity: plain hook and GM fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE player-arg\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"HookPly = nil\n"
			"hook.Add( 'PlayerInitialSpawn', 'rec', function( ply ) HookPly = ply end )" );
		RunInitialSpawn( L );
		lua_getglobal( L, "HookPly" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "plain hook receives the player" );
		lua_pop( L, 2 );
		luaL_dostring( L, "hook.Remove( 'PlayerInitialSpawn', 'rec' )" );

		luaL_dostring( L,
			"GAMEMODE = { PlayerInitialSpawn = function( gm, ply ) GMSelf = rawequal( gm, GAMEMODE ); GMPly = ply end }" );
		RunInitialSpawn( L );
		lua_getglobal( L, "GMSelf" );
		CHECK( lua_toboolean( L, -1 ), "GM called with itself" );
		lua_pop( L, 1 );
		lua_getglobal( L, "GMPly" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "GM fallback receives the player" );
		lua_pop( L, 2 );
		CHECK( sentinelOk( L, baseline ), "player-arg stack" );
	}

	//-----------------------------------------------------------------------
	// 3. distinct from PlayerSpawn: no cross-firing in either direction
	//-----------------------------------------------------------------------
	std::printf( "PHASE distinct\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"InitialFired = 0; SpawnFired = 0\n"
			"GAMEMODE = {\n"
			"	PlayerInitialSpawn = function() InitialFired = InitialFired + 1 end,\n"
			"	PlayerSpawn = function() SpawnFired = SpawnFired + 1 end }\n" );
		RunInitialSpawn( L );
		lua_getglobal( L, "InitialFired" );
		CHECK( lua_tointeger( L, -1 ) == 1, "initial fired once" );
		lua_pop( L, 1 );
		lua_getglobal( L, "SpawnFired" );
		CHECK( lua_tointeger( L, -1 ) == 0, "initial did not fire PlayerSpawn" );
		lua_pop( L, 1 );
		RunSpawn( L );
		lua_getglobal( L, "SpawnFired" );
		CHECK( lua_tointeger( L, -1 ) == 1, "spawn event works independently" );
		lua_pop( L, 1 );
		lua_getglobal( L, "InitialFired" );
		CHECK( lua_tointeger( L, -1 ) == 1, "spawn did not fire PlayerInitialSpawn" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "distinct stack" );
	}

	//-----------------------------------------------------------------------
	// 4. Lua errors: contained, reported, stack balanced
	//-----------------------------------------------------------------------
	std::printf( "PHASE errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerInitialSpawn = function() error( 'expected gm initial error' ) end }" );
		RunInitialSpawn( L );
		CHECK( reportedErrors == 1, "raising GM reported once" );
		CHECK( std::strstr( lastMessage, "expected gm initial error" ), "raising GM message" );
		CHECK( sentinelOk( L, baseline ), "raising GM stack" );

		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = nil\n"
			"Ran = false\n"
			"hook.Add( 'PlayerInitialSpawn', 'bad', function() error( 'hook initial error' ) end )\n"
			"hook.Add( 'PlayerInitialSpawn', 'good', function() Ran = true end )" );
		RunInitialSpawn( L );
		CHECK( reportedErrors == 0, "hook error contained by hook.lua" );
		lua_getglobal( L, "Ran" );
		CHECK( lua_toboolean( L, -1 ), "chain continued after erroring hook" );
		lua_pop( L, 1 );
		luaL_dostring( L, "hook.Remove( 'PlayerInitialSpawn', 'bad' ); hook.Remove( 'PlayerInitialSpawn', 'good' )" );
		CHECK( sentinelOk( L, baseline ), "errors stack" );
	}

	//-----------------------------------------------------------------------
	// 5. reconnect stability: repeated dispatches stay consistent
	//-----------------------------------------------------------------------
	std::printf( "PHASE repeat\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerInitialSpawn = function( gm, ply ) Count = ( Count or 0 ) + 1; Last = ply end }" );
		for( int i = 0; i < 1000; ++i )
			RunInitialSpawn( L );
		lua_getglobal( L, "Count" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "dispatch count" );
		lua_pop( L, 1 );
		lua_getglobal( L, "Last" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "player identity stable" );
		lua_pop( L, 2 );
		CHECK( sentinelOk( L, baseline ), "repeat stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "PlayerInitialSpawn dispatch OK" );
	std::fflush( stdout );
	return 0;
}
