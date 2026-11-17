// Regression for the base gamemode's first M4 slice: GM:PlayerLoadout
// replicating the vanilla multiplayer default spawn equipment.
//
// Loads the REAL files from the repository:
//   - lua/includes/modules/hook.lua (hook priority + GAMEMODE fallback)
//   - gamemodes/base/{base.txt, gamemode/shared.lua, gamemode/init.lua}
//     through the real C++ loading chain (XGLua_LoadGamemodeChain +
//     XGLua_ReadDiskRoot rooted at the repo)
// and drives the production decision helper XGLua_HookRunBool. The player
// is a stub whose Give/GiveAmmo methods mirror the real binding signatures
// (Ply_Give / Ply_GiveAmmo: Give(class), GiveAmmo(count, name, max)).
//
// Verified:
//   - chain loads the real base scripts; GAMEMODE.PlayerLoadout exists
//   - stub receives exactly weapon_crowbar, weapon_9mmhandgun and
//     GiveAmmo(68, "9mm", 250) in vanilla order
//   - the decision is a strict boolean true (C++ skips its defaults)
//   - without a selected gamemode the decision stays false (vanilla flow)
//   - stack and sentinel survive every path
//
// Build (ARM64, native; put the exe beside lua_shared.dll and make sure
// VCRUNTIME140.dll is installed or available beside the exe):
//   cl /nologo /EHsc /std:c++17 /utf-8 /I<luajit>/src /I<repo>/dlls/lua ^
//      /Fe:<luajit>/dist/arm64/base_loadout_test.exe base_loadout_test.cpp ^
//      /link /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
// Run from <repo>/tests/xglua so the module paths below resolve.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "xglua_gamemode.h"
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
static void onError( lua_State *L, const char *context )
{
	++reportedErrors;
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

// The production decision behind CHalfLifeMultiplay::PlayerSpawn's
// addDefault branch (XGLua_PlayerLoadout minus the entity pusher).
static bool LoadoutDecision( lua_State *L )
{
	return XGLua_HookRunBool( L, "PlayerLoadout", PushPlayer, NULL, onError );
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
	// the chain registers modes through gamemode.Register
	if( luaL_loadfile( L, "../../lua/includes/modules/gamemode.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "FAIL: gamemode.lua load: %s\n", lua_tostring( L, -1 ) );
		return 1;
	}
	luaL_dostring( L, "GAMEMODE = nil" );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. without a selected gamemode the vanilla flow stays untouched
	//-----------------------------------------------------------------------
	std::printf( "PHASE no-selection\n" ); std::fflush( stdout );
	{
		CHECK( !LoadoutDecision( L ), "no gamemode must keep vanilla defaults" );
		// the loader itself reports NO_SELECTION and touches nothing
		char err[256];
		CHECK( XGLua_LoadGamemodeChain( L, XGLua_ReadDiskRoot, (void *)"../..", "", err, sizeof( err ) )
			== XGLUA_GM_NO_SELECTION, "empty xgl_gamemode keeps current behavior" );
		CHECK( sentinelOk( L, baseline ), "no-selection stack" );
	}

	//-----------------------------------------------------------------------
	// 2. load the real base gamemode through the real chain
	//-----------------------------------------------------------------------
	std::printf( "PHASE load-real-base\n" ); std::fflush( stdout );
	{
		char err[256];
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, XGLua_ReadDiskRoot, (void *)"../..",
			"base", err, sizeof( err ) );
		if( r != XGLUA_GM_OK )
			std::printf( "chain error: %s\n", err );
		CHECK( r == XGLUA_GM_OK, "real base chain loads" );
		lua_getglobal( L, "GAMEMODE" );
		CHECK( lua_istable( L, -1 ), "GAMEMODE installed" );
		lua_getfield( L, -1, "Name" );
		CHECK( lua_isstring( L, -1 ) && std::strcmp( lua_tostring( L, -1 ), "Base" ) == 0,
			"shared.lua metadata visible" );
		lua_pop( L, 2 );
		luaL_dostring( L, "HasFn = type( GAMEMODE.PlayerLoadout ) == 'function'\n"
			"    and rawequal( GAMEMODE, gamemode.Get( 'base' ) )" );
		lua_getglobal( L, "HasFn" );
		CHECK( lua_toboolean( L, -1 ), "GM:PlayerLoadout exists" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "load-real-base stack" );
	}

	//-----------------------------------------------------------------------
	// 3. stub player: exact loadout, vanilla order, strict true
	//-----------------------------------------------------------------------
	std::printf( "PHASE stub-loadout\n" ); std::fflush( stdout );
	{
		if( luaL_loadstring( L,
			"local ply = { weapons = {}, ammoCalls = {} }\n"
			"function ply:Give( cls ) self.weapons[ #self.weapons + 1 ] = cls end\n"
			"function ply:GiveAmmo( count, name, max ) self.ammoCalls[ #self.ammoCalls + 1 ] = { count, name, max } end\n"
			"return ply" ) || lua_pcall( L, 0, 1, 0 ) )
		{
			std::printf( "FAIL: stub: %s\n", lua_tostring( L, -1 ) );
			return 1;
		}
		lua_pushvalue( L, -1 );
		lua_setglobal( L, "Stub" );	// for the dostring snippets below
		playerRef = luaL_ref( L, LUA_REGISTRYINDEX );

		// direct call: strict boolean true return
		luaL_dostring( L,
			"local ply = Stub\n"
			"local ok, a, b = pcall( GAMEMODE.PlayerLoadout, GAMEMODE, ply )\n"
			"DirectOk = ok and type( a ) == 'boolean' and a == true and b == nil\n"
			"VanillaOrder = ply.weapons[1] == 'weapon_crowbar'\n"
			"    and ply.weapons[2] == 'weapon_9mmhandgun' and #ply.weapons == 2\n"
			"AmmoOk = #ply.ammoCalls == 1\n"
			"    and ply.ammoCalls[1][1] == 68 and ply.ammoCalls[1][2] == '9mm'\n"
			"    and ply.ammoCalls[1][3] == 250" );
		lua_getglobal( L, "DirectOk" );
		CHECK( lua_toboolean( L, -1 ), "direct call returns strict boolean true" );
		lua_pop( L, 1 );
		lua_getglobal( L, "VanillaOrder" );
		CHECK( lua_toboolean( L, -1 ), "weapons: crowbar then 9mmhandgun, nothing else" );
		lua_pop( L, 1 );
		lua_getglobal( L, "AmmoOk" );
		CHECK( lua_toboolean( L, -1 ), "ammo: one call 68/'9mm'/250" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "stub-loadout stack" );

		// production decision path on a fresh stub: hook.Run -> GM fallback
		luaL_dostring( L, "Stub = { weapons = {}, ammoCalls = {}, giveCount = 0 }\n"
			"function Stub:Give( cls ) self.giveCount = self.giveCount + 1 self.weapons[ #self.weapons + 1 ] = cls end\n"
			"function Stub:GiveAmmo( count, name, max ) self.ammoCalls[ #self.ammoCalls + 1 ] = { count, name, max } end" );
		luaL_unref( L, LUA_REGISTRYINDEX, playerRef );
		lua_getglobal( L, "Stub" );
		playerRef = luaL_ref( L, LUA_REGISTRYINDEX );	// refs and pops

		reportedErrors = 0;
		CHECK( LoadoutDecision( L ), "decision true with real base loaded" );
		CHECK( reportedErrors == 0, "no Lua errors on the decision path" );
		luaL_dostring( L,
			"Effects = Stub.giveCount == 2 and #Stub.ammoCalls == 1\n"
			"    and Stub.ammoCalls[1][3] == 250\n"
			"    and Stub.weapons[1] == 'weapon_crowbar' and Stub.weapons[2] == 'weapon_9mmhandgun'" );
		lua_getglobal( L, "Effects" );
		CHECK( lua_toboolean( L, -1 ), "hook fallback ran the loadout exactly once" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "decision stack" );
	}

	//-----------------------------------------------------------------------
	// 4. unloading the gamemode restores the vanilla default behavior
	//-----------------------------------------------------------------------
	std::printf( "PHASE unload\n" ); std::fflush( stdout );
	{
		luaL_dostring( L, "GAMEMODE = nil; GM = nil; Stub = nil" );
		CHECK( !LoadoutDecision( L ), "cleared gamemode falls back to vanilla" );
		CHECK( sentinelOk( L, baseline ), "unload stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "base gamemode loadout OK" );
	std::fflush( stdout );
	return 0;
}
