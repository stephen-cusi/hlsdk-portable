// Regression for the PlayerDisconnected and PlayerSay server hooks.
// Loads the REAL lua/includes/modules/hook.lua from the repository and
// drives the production dispatch helpers:
//   - XGLua_HookRunProtected for PlayerDisconnected (single player arg)
//   - XGLua_HookRunString for PlayerSay (ply, text, teamonly) with the same
//     result mapping XGLua_PlayerSay applies (non-empty string = replaced,
//     empty string = swallowed, anything else = passthrough)
//
// Verified:
//   - PlayerDisconnected: player identity via hook and GM fallback, missing
//     handlers silent, errors contained, stack balance
//   - PlayerSay: argument identity and teamonly flag for say / say_team,
//     hook priority over the GM fallback, no-result / replaced / swallowed /
//     non-string results, hook errors contained, GM errors reported with
//     passthrough, long-string truncation
//
// Build & run from <repo>/tests/xglua (so hook.lua's relative path resolves).
//   /MT removes the test exe's CRT dependency, but lua_shared.dll may itself
//   require VCRUNTIME140.dll; install the matching runtime or place it beside
//   the exe. lua_shared.dll must also sit beside the exe.
//
//   cl /nologo /W3 /EHsc /std:c++17 /utf-8 /MT /I<luajit>/src ^
//      /I<repo>/dlls/lua /Fe:player_say_test.exe player_say_test.cpp /link ^
//      /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
//   copy <luajit>\dist\arm64\lua_shared.dll .
//   copy <VS-redist>\arm64\Microsoft.VC143.CRT\vcruntime140.dll . (if not installed)
//   player_say_test.exe

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

// Mirror of XGLua_PushSayArgs: (ply, text, teamonly).
struct SayArgs { const char *text; int teamonly; };
static int PushSayArgs( lua_State *L, void *ctx )
{
	SayArgs *a = (SayArgs *)ctx;
	lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
	lua_pushstring( L, a->text );
	lua_pushboolean( L, a->teamonly );
	return 3;
}

// Result codes mirroring XGLuaSayResult.
enum { SAY_PASSTHROUGH = 0, SAY_REPLACED, SAY_SWALLOWED };

// Mirror of XGLua_PlayerSay's decision mapping over the production
// XGLua_HookRunString (the text source differs: tests inject it directly).
static int DecideSay( lua_State *L, const char *text, int teamonly, char *out, size_t outlen )
{
	SayArgs args = { text, teamonly };
	char result[256];
	if( !XGLua_HookRunString( L, "PlayerSay", PushSayArgs, &args, result, sizeof( result ), onError ) )
		return SAY_PASSTHROUGH;
	if( !result[0] )
		return SAY_SWALLOWED;
	snprintf( out, outlen, "%s", result );
	return SAY_REPLACED;
}

static void RunDisconnected( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerDisconnected", PushPlayer, NULL, onError );
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
	char out[128];

	//-----------------------------------------------------------------------
	// 1. PlayerDisconnected: missing handlers stay silent
	//-----------------------------------------------------------------------
	std::printf( "PHASE disc-missing\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		RunDisconnected( L );
		CHECK( reportedErrors == 0, "no hook, no GM must stay silent" );
		CHECK( sentinelOk( L, baseline ), "disc-missing stack" );
	}

	//-----------------------------------------------------------------------
	// 2. PlayerDisconnected: player identity via hook and GM fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE disc-args\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"HookPly = nil\n"
			"hook.Add( 'PlayerDisconnected', 'rec', function( ply ) HookPly = ply end )" );
		RunDisconnected( L );
		lua_getglobal( L, "HookPly" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "hook receives the player" );
		lua_pop( L, 2 );
		luaL_dostring( L, "hook.Remove( 'PlayerDisconnected', 'rec' )" );

		luaL_dostring( L,
			"GAMEMODE = { PlayerDisconnected = function( gm, ply ) GMSelf = rawequal( gm, GAMEMODE ); GMPly = ply end }" );
		RunDisconnected( L );
		lua_getglobal( L, "GMSelf" );
		CHECK( lua_toboolean( L, -1 ), "GM called with itself" );
		lua_pop( L, 1 );
		lua_getglobal( L, "GMPly" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "GM fallback receives the player" );
		lua_pop( L, 2 );
		CHECK( sentinelOk( L, baseline ), "disc-args stack" );
	}

	//-----------------------------------------------------------------------
	// 3. PlayerDisconnected: errors contained
	//-----------------------------------------------------------------------
	std::printf( "PHASE disc-errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerDisconnected = function() error( 'expected gm disconnect error' ) end }" );
		RunDisconnected( L );
		CHECK( reportedErrors == 1, "raising GM reported once" );
		CHECK( std::strstr( lastMessage, "expected gm disconnect error" ), "raising GM message" );
		CHECK( sentinelOk( L, baseline ), "raising GM stack" );
		luaL_dostring( L, "GAMEMODE = nil" );
	}

	//-----------------------------------------------------------------------
	// 4. PlayerSay: argument identity and teamonly flag
	//-----------------------------------------------------------------------
	std::printf( "PHASE say-args\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"Seen = nil\n"
			"hook.Add( 'PlayerSay', 'rec', function( ply, text, teamonly ) Seen = { ply, text, teamonly } end )" );
		CHECK( DecideSay( L, "hello world", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH,
			"recording hook returns nil -> passthrough" );
		lua_getglobal( L, "Seen" );
		const int seen = lua_gettop( L );
		lua_rawgeti( L, seen, 1 ); lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
		CHECK( lua_rawequal( L, -1, -2 ), "arg1 is the player" ); lua_pop( L, 2 );
		lua_rawgeti( L, seen, 2 );
		CHECK( lua_isstring( L, -1 ) && std::strcmp( lua_tostring( L, -1 ), "hello world" ) == 0,
			"arg2 is the raw text" ); lua_pop( L, 1 );
		lua_rawgeti( L, seen, 3 );
		CHECK( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ), "arg3 false for say" ); lua_pop( L, 1 );
		lua_pop( L, 1 );
		CHECK( DecideSay( L, "team msg", 1, out, sizeof( out ) ) == SAY_PASSTHROUGH, "say_team passthrough" );
		lua_getglobal( L, "Seen" );
		lua_rawgeti( L, -1, 3 );
		CHECK( lua_toboolean( L, -1 ), "arg3 true for say_team" );
		lua_pop( L, 2 );
		luaL_dostring( L, "hook.Remove( 'PlayerSay', 'rec' )" );
		CHECK( sentinelOk( L, baseline ), "say-args stack" );
	}

	//-----------------------------------------------------------------------
	// 5. PlayerSay result mapping: replace / swallow / passthrough
	//-----------------------------------------------------------------------
	std::printf( "PHASE say-results\n" ); std::fflush( stdout );
	{
		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function( ply, text ) return '[x] ' .. text end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_REPLACED, "string result replaces" );
		CHECK( std::strcmp( out, "[x] hi" ) == 0, "replaced text content" );

		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function() return '' end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_SWALLOWED, "empty string swallows" );

		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function() return nil end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH, "nil result passes through" );

		// non-string results: documented fallback, and because a non-nil
		// hook return terminates the chain, the GM fallback is suppressed
		luaL_dostring( L,
			"GAMEMODE = { PlayerSay = function() return 'gm text' end }\n"
			"hook.Add( 'PlayerSay', 't', function() return 42 end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH, "number result passes through" );
		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function() return { 1 } end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH, "table result passes through" );
		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function() return true end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH, "boolean result passes through" );

		// GM fallback with no hooks in the chain
		luaL_dostring( L, "hook.Remove( 'PlayerSay', 't' )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_REPLACED, "GM string replaces" );
		CHECK( std::strcmp( out, "gm text" ) == 0, "GM replaced text content" );

		// hook priority: a hook string beats the GM string
		luaL_dostring( L, "hook.Add( 'PlayerSay', 't', function() return 'hook wins' end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_REPLACED, "hook beats GM" );
		CHECK( std::strcmp( out, "hook wins" ) == 0, "hook priority content" );
		luaL_dostring( L, "hook.Remove( 'PlayerSay', 't' ); GAMEMODE = nil" );
		CHECK( sentinelOk( L, baseline ), "say-results stack" );
	}

	//-----------------------------------------------------------------------
	// 6. PlayerSay errors: contained with passthrough fallback
	//-----------------------------------------------------------------------
	std::printf( "PHASE say-errors\n" ); std::fflush( stdout );
	{
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerSay = function() error( 'expected gm say error' ) end }" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_PASSTHROUGH, "GM error falls back" );
		CHECK( reportedErrors == 1, "GM error reported once" );
		CHECK( std::strstr( lastMessage, "expected gm say error" ), "GM error message" );

		// hook-internal error: contained by hook.lua, chain continues to GM
		reportedErrors = 0;
		luaL_dostring( L,
			"GAMEMODE = { PlayerSay = function() return 'gm after bad hook' end }\n"
			"hook.Add( 'PlayerSay', 'bad', function() error( 'hook say error' ) end )" );
		CHECK( DecideSay( L, "hi", 0, out, sizeof( out ) ) == SAY_REPLACED, "chain continued after erroring hook" );
		CHECK( std::strcmp( out, "gm after bad hook" ) == 0, "fallback content after hook error" );
		CHECK( reportedErrors == 0, "hook error contained by hook.lua" );
		luaL_dostring( L, "hook.Remove( 'PlayerSay', 'bad' ); GAMEMODE = nil" );
		CHECK( sentinelOk( L, baseline ), "say-errors stack" );
	}

	//-----------------------------------------------------------------------
	// 7. truncation and stability
	//-----------------------------------------------------------------------
	std::printf( "PHASE truncation\n" ); std::fflush( stdout );
	{
		luaL_dostring( L,
			"hook.Add( 'PlayerSay', 't', function() return string.rep( 'a', 400 ) end )" );
		char big[128];
		CHECK( DecideSay( L, "hi", 0, big, sizeof( big ) ) == SAY_REPLACED, "long string still replaces" );
		CHECK( std::strlen( big ) == 127, "long string truncated to the caller buffer" );
		luaL_dostring( L, "hook.Remove( 'PlayerSay', 't' )" );

		luaL_dostring( L,
			"GAMEMODE = { PlayerSay = function( gm, ply ) Count = ( Count or 0 ) + 1 end,\n"
			"	PlayerDisconnected = function( gm, ply ) Disc = ( Disc or 0 ) + 1 end }" );
		for( int i = 0; i < 1000; ++i )
		{
			DecideSay( L, "m", 0, out, sizeof( out ) );
			RunDisconnected( L );
		}
		lua_getglobal( L, "Count" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "say dispatch count" );
		lua_pop( L, 1 );
		lua_getglobal( L, "Disc" );
		CHECK( lua_tointeger( L, -1 ) == 1000, "disconnect dispatch count" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "truncation stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "PlayerDisconnected + PlayerSay OK" );
	std::fflush( stdout );
	return 0;
}
