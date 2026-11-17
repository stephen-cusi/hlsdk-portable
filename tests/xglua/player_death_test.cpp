// Regression for hook.Run("PlayerDeath", victim, inflictor, attacker)
// wiring in CBasePlayer::Killed. Loads the REAL
// lua/includes/modules/hook.lua from the repository and drives the
// production XGLua_HookRunProtected dispatch with a mirror of the
// XGLua_PushDeathArgs pusher (3 arguments, invalid/empty entities mapped
// to nil exactly like XGLua_PushEntOrNull does with FNullEnt).
//
// Verified:
//   - argument order and identity: victim, inflictor, attacker reach both
//     plain hooks and the GM:PlayerDeath fallback (with gm as self)
//   - empty entity arguments arrive as nil (inflictor=NULL, attacker=NULL)
//   - missing hook and missing GAMEMODE stay silent no-ops
//   - a raising GM fallback is reported and contained; hook-internal errors
//     are contained by hook.lua itself
//   - stack top and sentinel survive every path
//
// Build (ARM64, native; /MT removes the test exe's CRT dependency):
//   cl /nologo /W3 /EHsc /std:c++17 /utf-8 /MT /I<luajit>/src ^
//      /I<repo>/dlls/lua /Fe:<luajit>/dist/arm64/player_death_test.exe ^
//      player_death_test.cpp /link ^
//      /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
// Run from <repo>/tests/xglua so hook.lua's relative path resolves. Keep
// lua_shared.dll beside the exe; that DLL may itself need VCRUNTIME140.dll
// installed or available beside it even though this test exe uses /MT.
//
// Without /MT, the test exe also needs VCRUNTIME140.dll.

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

// Three entity fixtures in the registry; 0 means "map to nil" (the mirror
// of FNullEnt failing in XGLua_PushEntOrNull).
static int refVictim = 0, refInflictor = 0, refAttacker = 0;

static void PushEntRefOrNull( lua_State *L, int ref )
{
	if( ref )
		lua_rawgeti( L, LUA_REGISTRYINDEX, ref );
	else
		lua_pushnil( L );
}

static int PushDeathArgsMirror( lua_State *L, void *ctx )
{
	(void)ctx;
	PushEntRefOrNull( L, refVictim );	// victim: the dying player (always valid)
	PushEntRefOrNull( L, refInflictor );	// g_pevLastInflictor, may be NULL/stale
	PushEntRefOrNull( L, refAttacker );	// pevAttacker, may be NULL/world
	return 3;
}

// The production dispatch behind XGLua_PlayerDeath.
static void RunPlayerDeath( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerDeath", PushDeathArgsMirror, NULL, onError );
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

	// entity fixtures: distinct tables with markers
	luaL_dostring( L, "return { tag = 'victim' }" );
	refVictim = luaL_ref( L, LUA_REGISTRYINDEX );
	luaL_dostring( L, "return { tag = 'inflictor' }" );
	refInflictor = luaL_ref( L, LUA_REGISTRYINDEX );
	luaL_dostring( L, "return { tag = 'attacker' }" );
	refAttacker = luaL_ref( L, LUA_REGISTRYINDEX );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. missing hook and missing gamemode: silent no-op
	//-----------------------------------------------------------------------
	std::printf( "PHASE missing\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		RunPlayerDeath( L );
		CHECK( reportedErrors == 0, "no hook, no GM must stay silent" );
		CHECK( sentinelOk( L, baseline ), "missing stack" );
	}

	//-----------------------------------------------------------------------
	// 2. argument order and identity through a plain hook
	//-----------------------------------------------------------------------
	std::printf( "PHASE hook-args\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"Seen = nil\n"
			"hook.Add( 'PlayerDeath', 'rec', function( victim, inflictor, attacker )\n"
			"	Seen = { n = 3, victim, inflictor, attacker } end )" );
		RunPlayerDeath( L );
		lua_getglobal( L, "Seen" );
		CHECK( lua_istable( L, -1 ), "hook recorded args" );
		const int seen = lua_gettop( L );
		struct { int idx; int ref; const char *msg; } checks[3] = {
			{ 1, refVictim, "arg1 is victim" },
			{ 2, refInflictor, "arg2 is inflictor" },
			{ 3, refAttacker, "arg3 is attacker" } };
		for( int c = 0; c < 3; ++c )
		{
			lua_rawgeti( L, seen, checks[c].idx );
			lua_rawgeti( L, LUA_REGISTRYINDEX, checks[c].ref );
			CHECK( lua_rawequal( L, -1, -2 ), checks[c].msg );
			lua_pop( L, 2 );
		}
		lua_pop( L, 1 );	// Seen
		luaL_dostring( L, "hook.Remove( 'PlayerDeath', 'rec' )" );
		CHECK( sentinelOk( L, baseline ), "hook-args stack" );
	}

	//-----------------------------------------------------------------------
	// 3. GAMEMODE fallback receives (self, victim, inflictor, attacker)
	//-----------------------------------------------------------------------
	std::printf( "PHASE gm-fallback\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerDeath = function( gm, victim, inflictor, attacker )\n"
			"	GMSelf = rawequal( gm, GAMEMODE )\n"
			"	GMArgs = { n = 3, victim, inflictor, attacker } end }" );
		RunPlayerDeath( L );
		lua_getglobal( L, "GMSelf" );
		CHECK( lua_toboolean( L, -1 ), "GM called with itself" );
		lua_pop( L, 1 );
		lua_getglobal( L, "GMArgs" );
		CHECK( lua_istable( L, -1 ), "GM recorded args" );
		lua_rawgeti( L, -1, 1 ); lua_rawgeti( L, LUA_REGISTRYINDEX, refVictim );
		CHECK( lua_rawequal( L, -1, -2 ), "GM arg1 is victim" ); lua_pop( L, 2 );
		lua_rawgeti( L, -1, 2 ); lua_rawgeti( L, LUA_REGISTRYINDEX, refInflictor );
		CHECK( lua_rawequal( L, -1, -2 ), "GM arg2 is inflictor" ); lua_pop( L, 2 );
		lua_rawgeti( L, -1, 3 ); lua_rawgeti( L, LUA_REGISTRYINDEX, refAttacker );
		CHECK( lua_rawequal( L, -1, -2 ), "GM arg3 is attacker" ); lua_pop( L, 2 );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "gm-fallback stack" );
	}

	//-----------------------------------------------------------------------
	// 4. empty entity arguments arrive as nil
	//-----------------------------------------------------------------------
	std::printf( "PHASE nil-args\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerDeath = function( gm, v, i, a ) NilMarks = { v ~= nil, i == nil, a == nil } end }" );
		int savedInflictor = refInflictor, savedAttacker = refAttacker;
		refInflictor = 0; refAttacker = 0;	// FNullEnt path
		RunPlayerDeath( L );
		refInflictor = savedInflictor; refAttacker = savedAttacker;
		lua_getglobal( L, "NilMarks" );
		lua_rawgeti( L, -1, 1 ); CHECK( lua_toboolean( L, -1 ), "victim still present" ); lua_pop( L, 1 );
		lua_rawgeti( L, -1, 2 ); CHECK( lua_toboolean( L, -1 ), "null inflictor mapped to nil" ); lua_pop( L, 1 );
		lua_rawgeti( L, -1, 3 ); CHECK( lua_toboolean( L, -1 ), "null attacker mapped to nil" ); lua_pop( L, 1 );
		lua_pop( L, 1 );
		// attacker == world-style: only attacker nil
		refAttacker = 0;
		luaL_dostring( L, "NilMarks = nil" );
		RunPlayerDeath( L );
		refAttacker = savedAttacker;
		lua_getglobal( L, "NilMarks" );
		lua_rawgeti( L, -1, 3 ); CHECK( lua_toboolean( L, -1 ), "attacker-only nil path" ); lua_pop( L, 1 );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "nil-args stack" );
	}

	//-----------------------------------------------------------------------
	// 5. Lua errors: contained, reported, stack balanced
	//-----------------------------------------------------------------------
	std::printf( "PHASE errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerDeath = function() error( 'expected gm death error' ) end }" );
		RunPlayerDeath( L );
		CHECK( reportedErrors == 1, "raising GM reported once" );
		CHECK( std::strstr( lastMessage, "expected gm death error" ), "raising GM message" );
		CHECK( sentinelOk( L, baseline ), "raising GM stack" );

		// hook-internal errors are contained by hook.lua; chain continues
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = nil\n"
			"Ran = false\n"
			"hook.Add( 'PlayerDeath', 'bad', function() error( 'hook death error' ) end )\n"
			"hook.Add( 'PlayerDeath', 'good', function() Ran = true end )" );
		RunPlayerDeath( L );
		CHECK( reportedErrors == 0, "hook error contained by hook.lua" );
		lua_getglobal( L, "Ran" );
		CHECK( lua_toboolean( L, -1 ), "chain continued after erroring hook" );
		lua_pop( L, 1 );
		luaL_dostring( L, "hook.Remove( 'PlayerDeath', 'bad' ); hook.Remove( 'PlayerDeath', 'good' )" );
		CHECK( sentinelOk( L, baseline ), "errors stack" );
	}

	//-----------------------------------------------------------------------
	// 6. repeated deaths: exactly one dispatch each, identity stable
	//-----------------------------------------------------------------------
	std::printf( "PHASE repeat\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"GAMEMODE = { PlayerDeath = function( gm, v ) Count = ( Count or 0 ) + 1; LastV = v end }" );
		for( int i = 0; i < 1000; ++i )
			RunPlayerDeath( L );
		lua_getglobal( L, "Count" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "one dispatch per death" );
		lua_pop( L, 1 );
		lua_getglobal( L, "LastV" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, refVictim );
		CHECK( lua_rawequal( L, -1, -2 ), "victim identity stable" );
		lua_pop( L, 2 );
		CHECK( sentinelOk( L, baseline ), "repeat stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "PlayerDeath dispatch OK" );
	std::fflush( stdout );
	return 0;
}
