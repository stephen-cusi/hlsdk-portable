// Regression for GM:PlayerLoadout(ply) control over the vanilla default
// spawn loadout. Drives the production decision helper XGLua_HookRunBool
// (dlls/lua/xglua_hook.h) with the REAL lua/includes/modules/hook.lua loaded
// from the repository, so hook priority, the GAMEMODE fallback, xpcall
// containment of hook errors, and the strict-boolean reduction are the
// shipped behaviors. The CHalfLifeMultiplay::PlayerSpawn wiring
// (game_player_equip priority, addDefault-only interception) is mirrored
// here from the patched branch; no test Lua lives under runtime autorun.
//
// Build (ARM64, native; put the exe beside lua_shared.dll and make sure
// VCRUNTIME140.dll is installed or available beside the exe):
//   cl /nologo /EHsc /std:c++17 /utf-8 /I<luajit>/src /I<repo>/dlls/lua ^
//      /Fe:<luajit>/dist/arm64/player_loadout_test.exe player_loadout_test.cpp ^
//      /link /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
// Run from <repo>/tests/xglua so hook.lua's relative path resolves.

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

static bool sentinelOk( lua_State *L, int baseline )
{
	return lua_gettop( L ) == baseline && lua_isstring( L, 1 ) &&
		std::strcmp( lua_tostring( L, 1 ), "sentinel" ) == 0;
}

// The production dispatch behind XGLua_PlayerLoadout (vm.cpp adapter passes
// the real entity pusher; everything else is this call).
static bool LoadoutDecision( lua_State *L )
{
	return XGLua_HookRunBool( L, "PlayerLoadout", PushPlayer, NULL, onError );
}

// Mirror of the patched CHalfLifeMultiplay::PlayerSpawn branch: returns true
// when the vanilla default set must be given, and counts Lua consultations.
static int luaConsulted = 0;
static bool SpawnBranchMirror( lua_State *L, bool hasEquipEntity )
{
	BOOL addDefault = TRUE;
	if( hasEquipEntity )
		addDefault = FALSE;	// game_player_equip keeps original priority

	if( addDefault )
	{
		++luaConsulted;
		if( !LoadoutDecision( L ) )
			return true;	// vanilla crowbar/9mm/ammo
		return false;		// Lua replaced the default set
	}
	return false;			// equip entity already supplied the loadout
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
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) ) return fail( "hook global missing" );
	lua_pop( L, 1 );

	if( luaL_dostring( L, "return { got = false }" ) ) return fail( "player fixture" );
	playerRef = luaL_ref( L, LUA_REGISTRYINDEX );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. no gamemode at all
	//-----------------------------------------------------------------------
	std::printf( "PHASE no-gamemode\n" ); std::fflush( stdout );
	{
		luaL_dostring( L, "hook.Remove( 'PlayerLoadout', 't' ); GAMEMODE = nil" );
		CHECK( !LoadoutDecision( L ), "no gamemode, no hooks -> defaults" );
		luaL_dostring( L, "hook.Add( 'PlayerLoadout', 't', function( ply ) ply.got = 'plain' return true end )" );
		CHECK( LoadoutDecision( L ), "plain hook true works without a gamemode" );
		luaL_dostring( L, "hook.Add( 'PlayerLoadout', 't', function( ply ) return false end )" );
		CHECK( !LoadoutDecision( L ), "plain hook false -> defaults" );
		luaL_dostring( L, "hook.Add( 'PlayerLoadout', 't', function( ply ) return nil end )" );
		CHECK( !LoadoutDecision( L ), "hook nil falls through to nothing" );
		luaL_dostring( L, "hook.Remove( 'PlayerLoadout', 't' )" );
		CHECK( sentinelOk( L, baseline ), "no-gamemode stack" );
	}

	//-----------------------------------------------------------------------
	// 2. GM:PlayerLoadout return handling (strict boolean)
	//-----------------------------------------------------------------------
	std::printf( "PHASE gm-returns\n" ); std::fflush( stdout );
	{
		const char *returns[] = { "true", "false", "nil", "1", "'yes'", "{}", NULL };
		const bool expect[] = { true, false, false, false, false, false };
		for( int i = 0; returns[i]; ++i )
		{
			char code[256];
			snprintf( code, sizeof( code ),
				"GAMEMODE = { PlayerLoadout = function( self, ply ) ply.gm = true return %s end }",
				returns[i] );
			if( luaL_dostring( L, code ) ) return fail( "GM fixture" );
			bool got = LoadoutDecision( L );
			CHECK( got == expect[i], returns[i] );
			// the GM function really ran with (self, ply)
			if( i == 0 )
			{
				lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
				lua_getfield( L, -1, "gm" );
				CHECK( lua_toboolean( L, -1 ), "GM called with ply" );
				lua_pop( L, 2 );
			}
		}
		luaL_dostring( L, "GAMEMODE = {}" );	// no method at all
		CHECK( !LoadoutDecision( L ), "GM without method -> defaults" );
		CHECK( sentinelOk( L, baseline ), "gm-returns stack" );
	}

	//-----------------------------------------------------------------------
	// 3. hook priority over the GM fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE priority\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() return false end }\n"
			"hook.Add( 'PlayerLoadout', 't', function() return true end )" );
		CHECK( LoadoutDecision( L ), "hook true overrides GM false" );
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() return true end }\n"
			"hook.Add( 'PlayerLoadout', 't', function() return false end )" );
		CHECK( !LoadoutDecision( L ), "hook false terminates chain before GM" );
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() return true end }\n"
			"hook.Add( 'PlayerLoadout', 't', function() return nil end )" );
		CHECK( LoadoutDecision( L ), "hook nil falls through to GM true" );
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() return true end }\n"
			"hook.Add( 'PlayerLoadout', 'first', function() return nil end )\n"
			"hook.Add( 'PlayerLoadout', 'second', function() return true end )" );
		CHECK( LoadoutDecision( L ), "later hook after nil-returning earlier one" );
		luaL_dostring( L, "hook.Remove( 'PlayerLoadout', 't' ); hook.Remove( 'PlayerLoadout', 'first' ); hook.Remove( 'PlayerLoadout', 'second' )" );
		CHECK( sentinelOk( L, baseline ), "priority stack" );
	}

	//-----------------------------------------------------------------------
	// 4. Lua errors: contained, reported, spawn-safe
	//-----------------------------------------------------------------------
	std::printf( "PHASE errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() error( 'expected gm loadout error' ) end }\n"
			"hook.Add( 'PlayerLoadout', 't', function() end )" );
		CHECK( !LoadoutDecision( L ), "raising GM -> defaults" );
		CHECK( reportedErrors == 1, "raising GM reported once" );
		CHECK( std::strstr( lastMessage, "expected gm loadout error" ), "raising GM message" );
		CHECK( sentinelOk( L, baseline ), "raising GM stack" );

		// errors inside a registered hook are contained by hook.lua itself:
		// the chain continues into the GM fallback
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function() return true end }\n"
			"hook.Add( 'PlayerLoadout', 'bad', function() error( 'hook blows up' ) end )" );
		CHECK( LoadoutDecision( L ), "erroring hook falls back to GM true" );
		CHECK( reportedErrors == 0, "hook error did not escape hook.lua" );
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = nil\n"
			"hook.Add( 'PlayerLoadout', 'bad', function() error( 'hook blows up' ) end )" );
		CHECK( !LoadoutDecision( L ), "erroring hook without GM -> defaults" );
		luaL_dostring( L, "hook.Remove( 'PlayerLoadout', 'bad' )" );

		// hostile environment: missing/broken hook globals stay silent+false
		luaL_dostring( L, "SavedHook = hook; hook = nil; GAMEMODE = nil" );
		CHECK( !LoadoutDecision( L ), "missing hook global -> defaults" );
		luaL_dostring( L, "hook = { Run = 42 }" );
		CHECK( !LoadoutDecision( L ), "non-function Run -> defaults" );
		luaL_dostring( L, "hook = SavedHook" );
		CHECK( sentinelOk( L, baseline ), "errors stack" );
	}

	//-----------------------------------------------------------------------
	// 5. branch mirror: game_player_equip priority, no Lua consultation
	//-----------------------------------------------------------------------
	std::printf( "PHASE equip-priority\n" ); std::fflush( stdout );
	{
		luaL_dostring( L, "GAMEMODE = { PlayerLoadout = function() return true end }" );
		luaConsulted = 0;
		CHECK( !SpawnBranchMirror( L, true ), "equip branch must not give defaults" );
		CHECK( luaConsulted == 0, "equip branch consulted Lua" );
		CHECK( SpawnBranchMirror( L, false ) == false, "lua true skips defaults" );
		CHECK( luaConsulted == 1, "addDefault branch consulted Lua once" );
		luaL_dostring( L, "GAMEMODE = {}" );
		CHECK( SpawnBranchMirror( L, false ) == true, "lua fallthrough gives defaults" );
		CHECK( sentinelOk( L, baseline ), "equip-priority stack" );
	}

	//-----------------------------------------------------------------------
	// 6. repeated spawns stay stable (hook state across calls)
	//-----------------------------------------------------------------------
	std::printf( "PHASE repeat\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerLoadout = function( self, ply ) ply.count = ( ply.count or 0 ) + 1 return true end }" );
		for( int i = 0; i < 1000; ++i )
			CHECK( LoadoutDecision( L ), "repeat decision true" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		lua_getfield( L, -1, "count" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "GM ran every spawn" );
		lua_pop( L, 2 );
		CHECK( sentinelOk( L, baseline ), "repeat stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "PlayerLoadout decision OK" );
	std::fflush( stdout );
	return 0;
}
