// Regression for the PlayerCanPickupWeapon / PlayerCanPickupItem pickup
// vetoes. Loads the REAL lua/includes/modules/hook.lua from the repository
// and drives the production deny helper XGLua_HookDenyBool (the same
// protected tri-state core behind XGLua_PickupVeto in xglua_vm.cpp) with a
// mirror of the XGLua_PushPickupArgs two-entity pusher.
//
// Decision contract under test: only a strict Lua false denies. true, nil,
// non-boolean returns, no handlers at all, and reported Lua errors all keep
// the native allow behavior. Hook priority and the GAMEMODE fallback stay
// inside hook.Run; hook-internal errors are contained by hook.lua and the
// chain continues.
//
// Build & run from <repo>/tests/xglua. /MT removes the test exe's CRT
// dependency, but lua_shared.dll may itself need VCRUNTIME140.dll; install
// the matching runtime or place it beside the exe. Keep lua_shared.dll
// beside the exe too.
//
//   cl /nologo /W3 /EHsc /std:c++17 /utf-8 /MT /I<luajit>/src ^
//      /I<repo>/dlls/lua /Fe:player_pickup_test.exe player_pickup_test.cpp /link ^
//      /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
//   copy <luajit>\dist\arm64\lua_shared.dll .
//   copy <VS-redist>\arm64\Microsoft.VC143.CRT\vcruntime140.dll . (if not installed)
//   player_pickup_test.exe

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

static int playerRef = 0, itemRef = 0;

// Mirror of XGLua_PushPickupArgs: (ply, wep/item).
static int PushPickupArgs( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
	lua_rawgeti( L, LUA_REGISTRYINDEX, itemRef );
	return 2;
}

// Mirror of XGLua_PickupVeto: TRUE = allow (native), FALSE = deny.
static bool AllowPickup( lua_State *L, const char *event )
{
	return !XGLua_HookDenyBool( L, event, PushPickupArgs, NULL, onError );
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
	luaL_dostring( L, "return { tag = 'entity' }" );
	itemRef = luaL_ref( L, LUA_REGISTRYINDEX );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. no handlers: both events allow (native behavior)
	//-----------------------------------------------------------------------
	std::printf( "PHASE allow-default\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		CHECK( AllowPickup( L, "PlayerCanPickupWeapon" ), "weapon allowed by default" );
		CHECK( AllowPickup( L, "PlayerCanPickupItem" ), "item allowed by default" );
		CHECK( reportedErrors == 0, "silent without handlers" );
		CHECK( sentinelOk( L, baseline ), "allow-default stack" );
	}

	//-----------------------------------------------------------------------
	// 2. argument identity: (ply, wep/item) via hook and GM fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE args\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"Seen = nil\n"
			"hook.Add( 'PlayerCanPickupWeapon', 'rec', function( ply, wep ) Seen = { ply, wep } end )" );
		AllowPickup( L, "PlayerCanPickupWeapon" );
		lua_getglobal( L, "Seen" );
		const int seen = lua_gettop( L );
		lua_rawgeti( L, seen, 1 ); lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "weapon arg1 is the player" ); lua_pop( L, 2 );
		lua_rawgeti( L, seen, 2 ); lua_rawgeti( L, LUA_REGISTRYINDEX, itemRef );
		CHECK( lua_rawequal( L, -1, -2 ), "weapon arg2 is the weapon entity" ); lua_pop( L, 2 );
		lua_pop( L, 1 );
		luaL_dostring( L, "hook.Remove( 'PlayerCanPickupWeapon', 'rec' )" );

		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupItem = function( gm, ply, item ) GMSelf = rawequal( gm, GAMEMODE ); SeenGM = { ply, item } end }" );
		AllowPickup( L, "PlayerCanPickupItem" );
		lua_getglobal( L, "GMSelf" );
		CHECK( lua_toboolean( L, -1 ), "GM called with itself" );
		lua_pop( L, 1 );
		lua_getglobal( L, "SeenGM" );
		const int seenGM = lua_gettop( L );
		lua_rawgeti( L, seenGM, 1 ); lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "item GM arg1 is the player" ); lua_pop( L, 2 );
		lua_rawgeti( L, seenGM, 2 ); lua_rawgeti( L, LUA_REGISTRYINDEX, itemRef );
		CHECK( lua_rawequal( L, -1, -2 ), "item GM arg2 is the item entity" ); lua_pop( L, 2 );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "args stack" );
	}

	//-----------------------------------------------------------------------
	// 3. strict false denies; everything else allows
	//-----------------------------------------------------------------------
	std::printf( "PHASE strict-false\n" ); std::fflush( stdout );
	{
		const char *ret[] = { "false", "true", "nil", "0", "'no'", "{}", "1", NULL };
		const bool deny[] = { true, false, false, false, false, false, false };
		for( int i = 0; ret[i]; ++i )
		{
			char code[256];
			snprintf( code, sizeof( code ),
				"hook.Add( 'PlayerCanPickupWeapon', 't', function() return %s end )", ret[i] );
			luaL_dostring( L, code );
			snprintf( code, sizeof( code ),
				"GAMEMODE = { PlayerCanPickupItem = function() return %s end }", ret[i] );
			luaL_dostring( L, code );
			bool w = AllowPickup( L, "PlayerCanPickupWeapon" );
			bool it = AllowPickup( L, "PlayerCanPickupItem" );
			CHECK( ( !w ) == deny[i], ret[i] );
			CHECK( ( !it ) == deny[i], ret[i] );
		}
		luaL_dostring( L, "hook.Remove( 'PlayerCanPickupWeapon', 't' ); GAMEMODE = nil" );
		CHECK( sentinelOk( L, baseline ), "strict-false stack" );
	}

	//-----------------------------------------------------------------------
	// 4. hook priority over the GM fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE priority\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupWeapon = function() return true end }\n"
			"hook.Add( 'PlayerCanPickupWeapon', 't', function() return false end )" );
		CHECK( !AllowPickup( L, "PlayerCanPickupWeapon" ), "hook false beats GM true" );
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupWeapon = function() return false end }\n"
			"hook.Add( 'PlayerCanPickupWeapon', 't', function() return nil end )" );
		CHECK( !AllowPickup( L, "PlayerCanPickupWeapon" ), "hook nil falls through to GM false" );
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupWeapon = function() return false end }\n"
			"hook.Add( 'PlayerCanPickupWeapon', 't', function() return true end )" );
		CHECK( AllowPickup( L, "PlayerCanPickupWeapon" ), "hook true suppresses GM false (allow)" );
		// non-boolean hook return also terminates the chain -> allow
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupWeapon = function() return false end }\n"
			"hook.Add( 'PlayerCanPickupWeapon', 't', function() return 'text' end )" );
		CHECK( AllowPickup( L, "PlayerCanPickupWeapon" ), "non-boolean hook return allows" );
		luaL_dostring( L, "hook.Remove( 'PlayerCanPickupWeapon', 't' ); GAMEMODE = nil" );
		CHECK( sentinelOk( L, baseline ), "priority stack" );
	}

	//-----------------------------------------------------------------------
	// 5. Lua errors: reported with allow fallback; contained in hooks
	//-----------------------------------------------------------------------
	std::printf( "PHASE errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupItem = function() error( 'expected gm veto error' ) end }" );
		CHECK( AllowPickup( L, "PlayerCanPickupItem" ), "GM error falls back to allow" );
		CHECK( reportedErrors == 1, "GM error reported once" );
		CHECK( std::strstr( lastMessage, "expected gm veto error" ), "GM error message" );

		// hook-internal error: contained by hook.lua, chain reaches the GM
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerCanPickupItem = function() return false end }\n"
			"hook.Add( 'PlayerCanPickupItem', 'bad', function() error( 'hook veto error' ) end )" );
		CHECK( !AllowPickup( L, "PlayerCanPickupItem" ), "chain continued to GM after hook error" );
		CHECK( reportedErrors == 0, "hook error contained by hook.lua" );
		luaL_dostring( L, "hook.Remove( 'PlayerCanPickupItem', 'bad' ); GAMEMODE = nil" );
		CHECK( sentinelOk( L, baseline ), "errors stack" );
	}

	//-----------------------------------------------------------------------
	// 6. stability across repeated touches
	//-----------------------------------------------------------------------
	std::printf( "PHASE repeat\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = {\n"
			"	PlayerCanPickupWeapon = function( gm, ply, wep ) W = ( W or 0 ) + 1 end,\n"
			"	PlayerCanPickupItem = function( gm, ply, item ) I = ( I or 0 ) + 1 return false end }" );
		for( int i = 0; i < 1000; ++i )
		{
			CHECK( AllowPickup( L, "PlayerCanPickupWeapon" ), "weapon allow stable" );
			CHECK( !AllowPickup( L, "PlayerCanPickupItem" ), "item deny stable" );
		}
		lua_getglobal( L, "W" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "weapon dispatch count" );
		lua_pop( L, 1 );
		lua_getglobal( L, "I" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "item dispatch count" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "repeat stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "PlayerCanPickupWeapon + PlayerCanPickupItem OK" );
	std::fflush( stdout );
	return 0;
}
