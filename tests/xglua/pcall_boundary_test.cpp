// Regression for the XGLua protected-call boundary (xglua_safe.h and every
// engine-to-Lua entry built on it). Links the real ARM64 LuaJIT (lua_shared)
// and drives the production headers directly; only game-side symbols are
// stubbed. Every scenario asserts the Lua stack top, the stack sentinel and
// the registry sentinel survive each success and failure path.
//
// Lua 5.1 calling convention note: the callee is pushed first and the
// arguments land on top of it; lua_call/lua_pcall(n) take the func from
// top-(n+1). The dispatchers under test follow the production layout.
//
// Build (ARM64, native; put the exe beside lua_shared.dll so Windows loads it):
//   cl /nologo /EHsc /std:c++17 /utf-8 /I<luajit>/src /I<repo>/dlls/lua ^
//      /Fe:<luajit>/dist/arm64/pcall_boundary_test.exe ^
//      pcall_boundary_test.cpp /link /LIBPATH:<luajit>/dist/arm64 lua_shared.lib

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// Hard timeout: a hang or silent death anywhere below still reports. The
// whole suite normally finishes in well under a second.
static DWORD WINAPI Watchdog( LPVOID )
{
	Sleep( 120000 );
	std::printf( "WATCHDOG: timeout after 120s\n" );
	std::fflush( stdout );
	_exit( 2 );
	return 0;
}

// Production stub: the VM layer keeps the SWEP registry in a fixed table;
// tests reference it by a luaL_ref index like swep_bridge_test.cpp.
static int registryRef = 0;
void XGLua_GetSWEPRegistry( lua_State *L ) { lua_rawgeti( L, LUA_REGISTRYINDEX, registryRef ); }

#include "xglua_hook.h"
#include "xglua_vm_timer.h"
#include "xglua_swep_bridge.h"
#include "xglua_vm_include.h"

static int reportedErrors = 0;
static char lastContext[128] = "";
static char lastMessage[256] = "";

static void onError( lua_State *L, const char *context )
{
	++reportedErrors;
	snprintf( lastContext, sizeof( lastContext ), "%s", context ? context : "" );
	// production type-safety contract: only strings convert, non-string
	// error values degrade to their type name
	snprintf( lastMessage, sizeof( lastMessage ), "%s", XGLua_ErrorText( L, -1 ) );
	lua_pop( L, 1 );
}

static void phase( const char *name )
{
	std::printf( "PHASE %s\n", name );
	std::fflush( stdout );
}

static int fail( const char *what )
{
	std::printf( "FAIL: %s\n", what );
	std::fflush( stdout );
	return 1;
}

static bool sentinelOk( lua_State *L, int baseline )
{
	if( lua_gettop( L ) != baseline ) return false;
	if( !lua_isstring( L, 1 ) || std::strcmp( lua_tostring( L, 1 ), "sentinel" ) ) return false;
	lua_rawgeti( L, LUA_REGISTRYINDEX, registryRef );
	bool registryOk = lua_istable( L, -1 );
	lua_pop( L, 1 );
	return registryOk;
}

// Runs setup, then dispatch repeatedly, checking stack + sentinels. Returns
// the number of error reports during the runs, -1 on setup failure, -2 on
// an invariant violation.
static int run( lua_State *L, const char *setup, int reps, void ( *dispatch )( lua_State * ) )
{
	if( setup && luaL_dostring( L, setup ) )
	{
		std::printf( "FAIL: setup: %s\n", lua_tostring( L, -1 ) );
		std::fflush( stdout );
		lua_pop( L, 1 );
		return -1;
	}
	const int baseline = lua_gettop( L );
	reportedErrors = 0;
	for( int i = 0; i < reps; ++i )
		dispatch( L );
	if( !sentinelOk( L, baseline ) )
		return -2;
	return reportedErrors;
}

//===========================================================================
// dispatchers - thin mirrors of the production call sites
//===========================================================================

static void dispatchThink( lua_State *L )
{
	XGLua_HookRunProtected( L, "Think", NULL, NULL, onError );
}

static void dispatchTimer( lua_State *L )
{
	XGLua_RunTimerTick( L, onError );
}

// PlayerSpawn-style dispatch: the entity argument pushed inside the
// protected frame, after the event argument (func-below-args protocol).
static int selfRef = 0;
static int PushSelf( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_rawgeti( L, LUA_REGISTRYINDEX, selfRef );
	return 1;
}
static void dispatchSpawn( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerSpawn", PushSelf, NULL, onError );
}

// Arg pusher that raises: argument allocation failures must stay protected.
static int ThrowingPusher( lua_State *L, void * )
{
	return lua_error( L );
}
static void dispatchSpawnThrowingPusher( lua_State *L )
{
	XGLua_HookRunProtected( L, "PlayerSpawn", ThrowingPusher, NULL, onError );
}

// SWEP dispatch through the production bridge, with the entity argument.
static void dispatchSWEP( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_grenadepistol", "PrimaryAttack", PushSelf, NULL, onError, "SWEP:PrimaryAttack" );
}
static void dispatchSWEPMissing( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_grenadepistol", "NoSuchMethod", PushSelf, NULL, onError, "SWEP:NoSuchMethod" );
}
static void dispatchSWEPGhostClass( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_ghost", "PrimaryAttack", PushSelf, NULL, onError, "SWEP:PrimaryAttack" );
}
static void dispatchSWEPBrokenClass( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_broken", "PrimaryAttack", PushSelf, NULL, onError, "SWEP:PrimaryAttack" );
}
static void dispatchSWEPThrowing( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_grenadepistol", "Reload", PushSelf, NULL, onError, "SWEP:Reload" );
}
static void dispatchSWEPThrowingIndex( lua_State *L )
{
	XGLua_CallSWEPMethod( L, "weapon_throwing", "PrimaryAttack", PushSelf, NULL, onError, "SWEP:PrimaryAttack" );
}

// The Lua 5.1 call protocol puts the callee BELOW its arguments; a dispatch
// with the callee on top makes the first argument land in the func slot and
// raises "attempt to call a string value" (the exact failure once observed
// in this project). The wrong layout stays protected and stack-balanced;
// this pins the convention every production dispatcher relies on.
static int WrongLayoutCore( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_pushstring( L, "Think" );		// [event] would end up in the func slot
	lua_getglobal( L, "hook" );
	lua_getfield( L, -1, "Run" );
	lua_remove( L, -2 );				// [event, Run]: func slot holds the string
	if( lua_pcall( L, 1, 0, 0 ) != 0 )
		return lua_error( L );			// propagate into the test's error capture
	return 0;
}
static void dispatchWrongLayout( lua_State *L )
{
	XGLua_ProtectedCall( L, WrongLayoutCore, NULL, onError, "wronglayout" );
}

// Nested protection: an inner failing call is contained and the outer frame
// keeps working - the mechanism behind the per-SWEP cache isolation.
static int NestedInner( lua_State *L, void * )
{
	return lua_error( L );
}
// Lua 5.1 error() stringifies numbers (lua_isstring matches them), so a
// genuine numeric error object can only come from the C side.
static int RaiseNumberCore( lua_State *L, void * )
{
	lua_pushinteger( L, 42 );
	return lua_error( L );
}
static void dispatchNumberError( lua_State *L )
{
	XGLua_ProtectedCall( L, RaiseNumberCore, NULL, onError, "numerror" );
}
static int NestedOuterCore( lua_State *L, void *ctx )
{
	(void)ctx;
	if( XGLua_ProtectedCall( L, NestedInner, NULL, NULL, NULL ) )
		return lua_error( L );	// the inner call must have failed, not succeeded
	return 0;
}

struct CacheStackTestCtx { int tableRef; const char *classname; bool expectedAbstract; };
static int CacheStackCore( lua_State *L, void *ud )
{
	CacheStackTestCtx *ctx = (CacheStackTestCtx *)ud;
	if( lua_gettop( L ) != 0 )
	{
		lua_pushliteral( L, "guarded callback inherited the caller stack" );
		return lua_error( L );
	}
	XGLua_PushSWEPTableRef( L, ctx->tableRef );
	const int table = lua_gettop( L );
	if( XGLua_SWEPIsAbstract( L, table ) != ctx->expectedAbstract )
	{
		lua_pushliteral( L, "SWEP cache selected the wrong stack value" );
		return lua_error( L );
	}
	if( !XGLua_StoreSWEPTable( L, table, ctx->classname ) )
	{
		lua_pushliteral( L, "SWEP cache failed to store its table" );
		return lua_error( L );
	}
	return 0;
}

//===========================================================================
// fixtures
//===========================================================================

static bool RunSnippet( lua_State *L, const char *code, int nargs, const char **err )
{
	if( luaL_loadstring( L, code ) != 0 ) { *err = lua_tostring( L, -1 ); return false; }
	if( lua_pcall( L, nargs, 1, 0 ) != 0 ) { *err = lua_tostring( L, -1 ); return false; }
	return true;
}

static bool BuildSWEPFixtures( lua_State *L, const char **err )
{
	// self table the dispatchers push as the entity argument
	if( !RunSnippet( L, "return { shots = 0 }", 0, err ) ) return false;
	selfRef = luaL_ref( L, LUA_REGISTRYINDEX );

	XGLua_GetSWEPRegistry( L );		// stub pushes the registry table
	const int reg = lua_gettop( L );

	// parent weapon_base with two methods
	if( !RunSnippet( L, "return function(self) self.shots = self.shots + 1 end", 0, err ) ) return false;
	lua_newtable( L );											// [fn, parent]
	lua_pushvalue( L, -2 );
	lua_setfield( L, -2, "PrimaryAttack" );						// [fn, parent]
	if( !RunSnippet( L, "return function() error( 'expected swep error' ) end", 0, err ) ) return false;
	lua_setfield( L, -2, "Reload" );							// [fn, parent]
	lua_setfield( L, reg, "weapon_base" );						// [fn]
	lua_pop( L, 1 );

	// resolving child: __index is a Lua closure capturing the parent - the
	// same shape weapons.Register installs for Base inheritance
	lua_getfield( L, reg, "weapon_base" );						// [parent]
	if( luaL_loadstring( L,
		"local parent = ...\n"
		"local child = { Base = 'weapon_base' }\n"
		"setmetatable( child, { __index = function( t, k ) return parent[k] end } )\n"
		"return child\n" ) != 0 ) { *err = lua_tostring( L, -1 ); return false; }
	lua_insert( L, -2 );										// [chunk, parent]
	if( lua_pcall( L, 1, 1, 0 ) != 0 ) { *err = lua_tostring( L, -1 ); return false; }
	lua_setfield( L, reg, "weapon_grenadepistol" );

	// throwing child: __index raises on any lookup
	if( !RunSnippet( L, "return setmetatable( {}, { __index = function() error( 'swep meta' ) end } )", 0, err ) ) return false;
	lua_setfield( L, reg, "weapon_throwing" );

	// non-table registry entry
	lua_pushinteger( L, 42 );
	lua_setfield( L, reg, "weapon_broken" );

	lua_pop( L, 1 );	// registry
	return true;
}

// Memory reader for the production XGLua_ExecuteScript paths.
static XGLuaReadStatus MemRead( void *ctx, const char *rel, char **out, size_t *len )
{
	if( std::strcmp( rel, "missing.lua" ) == 0 ) return XGLUA_READ_MISSING;
	const char *src = (const char *)ctx;
	*len = std::strlen( src );
	*out = (char *)std::malloc( *len + 1 );
	if( !*out ) return XGLUA_READ_ERROR;
	std::memcpy( *out, src, *len + 1 );
	return XGLUA_READ_OK;
}

//===========================================================================
// main
//===========================================================================

int main()
{
	std::printf( "SMOKE: harness alive\n" );
	std::fflush( stdout );
	CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );
	HMODULE h = GetModuleHandleA( "lua_shared.dll" );
	char dll[MAX_PATH] = "";
	if( h && GetModuleFileNameA( h, dll, sizeof( dll ) ) )
		{ std::printf( "lua_shared: %s\n", dll ); std::fflush( stdout ); }

	lua_State *L = luaL_newstate();
	if( !L ) return fail( "new state" );
	luaL_openlibs( L );

	// stack slot 1: sentinel; registry stub set up before fixtures
	lua_pushliteral( L, "sentinel" );
	lua_newtable( L );
	registryRef = luaL_ref( L, LUA_REGISTRYINDEX );
	const char *err = NULL;
	if( !BuildSWEPFixtures( L, &err ) )
	{
		std::printf( "FAIL: fixtures: %s\n", err ? err : "?" );
		std::fflush( stdout );
		return 1;
	}

	const int baseline = lua_gettop( L );

	// --- nested cpcall reads and stores a table through a registry ref -------
	phase( "swep-cache-ref" );
	{
		const int cacheBase = lua_gettop( L );
		lua_newtable( L );
		lua_pushboolean( L, 1 );
		lua_setfield( L, -2, "Abstract" );
		lua_pushvalue( L, -1 );
		const int fixtureTableRef = luaL_ref( L, LUA_REGISTRYINDEX );
		const int fixtureTable = lua_gettop( L );
		CacheStackTestCtx ctx = { fixtureTableRef, "weapon_cache_fixture", true };
		if( !XGLua_ProtectedCall( L, CacheStackCore, &ctx, onError, "cache-stack" ) )
			return fail( "SWEP cache stack helper" );

		XGLua_GetSWEPRegistry( L );
		lua_pushliteral( L, "weapon_cache_fixture" );
		lua_rawget( L, -2 );
		if( !lua_rawequal( L, fixtureTable, -1 ) )
			return fail( "SWEP cache stored wrong stack value" );
		luaL_unref( L, LUA_REGISTRYINDEX, fixtureTableRef );
		lua_settop( L, cacheBase );
		if( !sentinelOk( L, baseline ) ) return fail( "SWEP cache stack/sentinel" );
	}

	// --- hook dispatch: silent-skip paths stay silent ---------------------
	phase( "hook-skip" );
	if( run( L, "hook = nil", 10000, dispatchThink ) != 0 ) return fail( "hook missing" );
	if( run( L, "hook = 42", 10000, dispatchThink ) != 0 ) return fail( "hook non-table" );
	if( run( L, "hook = {}", 10000, dispatchThink ) != 0 ) return fail( "hook Run missing" );
	if( run( L, "hook = { Run = 42 }", 10000, dispatchThink ) != 0 ) return fail( "hook Run non-function" );

	// --- hook dispatch: success and throwing callback ---------------------
	phase( "hook-run" );
	if( run( L, "count = 0; hook = { Run = function( ev ) count = count + 1 end }", 100000, dispatchThink ) != 0 )
		return fail( "hook success path" );
	lua_getglobal( L, "count" );
	if( lua_tointeger( L, -1 ) != 100000 ) return fail( "hook callback count" );
	lua_pop( L, 1 );

	if( run( L, "hook = { Run = function() error( 'expected hook error' ) end }", 10000, dispatchThink ) != 10000 )
		return fail( "hook callback error count" );
	if( std::strstr( lastMessage, "expected hook error" ) == NULL ) return fail( "hook error message" );
	if( std::strcmp( lastContext, "hook.Run(\"Think\")" ) != 0 ) return fail( "hook error context" );

	// --- stack-order convention regression --------------------------------
	phase( "stack-order" );
	if( run( L, "count = 0; hook = { Run = function() count = count + 1 end }", 1, dispatchWrongLayout ) != 1 )
		return fail( "wrong call layout not rejected" );
	if( std::strstr( lastMessage, "attempt to call a string value" ) == NULL ) return fail( "wrong layout error text" );
	lua_getglobal( L, "count" );
	if( lua_tointeger( L, -1 ) != 0 ) return fail( "wrong layout callback ran" );
	lua_pop( L, 1 );

	// --- non-string error objects degrade to type names -------------------
	if( run( L, "hook = { Run = function() error( {} ) end }", 10, dispatchThink ) != 10 ) return fail( "table error count" );
	if( std::strcmp( lastMessage, "table" ) != 0 ) return fail( "table error text" );
	{
		const int numBase = lua_gettop( L );
		if( XGLua_ProtectedCall( L, RaiseNumberCore, NULL, onError, "numerror" ) ) return fail( "number error not raised" );
		if( std::strcmp( lastMessage, "number" ) != 0 ) return fail( "number error text" );
		if( lua_gettop( L ) != numBase ) return fail( "number error stack" );
	}

	// --- nested protection containment ------------------------------------
	phase( "nested" );
	{
		const int nestedBase = lua_gettop( L );
		if( !XGLua_ProtectedCall( L, NestedOuterCore, NULL, onError, "nested-outer" ) )
			return fail( "nested outer failed" );
		if( lua_gettop( L ) != nestedBase ) return fail( "nested outer stack" );
	}

	// --- hook dispatch: throwing metamethods during lookup ----------------
	phase( "hook-meta" );
	if( run( L, "hook = setmetatable( {}, { __index = function( t, k ) if k == 'Run' then error( 'hook meta' ) end return rawget( t, k ) end } )",
		10000, dispatchThink ) != 10000 )
		return fail( "hook __index throw not protected" );
	if( std::strstr( lastMessage, "hook meta" ) == NULL ) return fail( "hook __index error message" );

	if( run( L, "hook = nil; setmetatable( _G, { __index = function( t, k ) if k == 'hook' then error( 'env meta' ) end return rawget( t, k ) end } )",
		10000, dispatchThink ) != 10000 )
		return fail( "_G __index throw not protected" );
	if( std::strstr( lastMessage, "env meta" ) == NULL ) return fail( "_G __index error message" );
	if( luaL_dostring( L, "setmetatable( _G, nil ); hook = nil" ) ) return fail( "reset _G metatable" );

	// --- PlayerSpawn-style dispatch: argument pushed inside protection ----
	phase( "spawn-args" );
	if( run( L, "spawned = 0; hook = { Run = function( ev, ply ) if type( ply ) == 'table' then spawned = spawned + 1 end end }",
		100000, dispatchSpawn ) != 0 )
		return fail( "PlayerSpawn success path" );
	lua_getglobal( L, "spawned" );
	if( lua_tointeger( L, -1 ) != 100000 ) return fail( "PlayerSpawn argument delivery" );
	lua_pop( L, 1 );

	if( run( L, "hook = { Run = function() end }", 10000, dispatchSpawnThrowingPusher ) != 10000 )
		return fail( "throwing arg pusher not protected" );
	if( run( L, "hook = { Run = function() error( 'spawn error' ) end }", 10000, dispatchSpawn ) != 10000 )
		return fail( "PlayerSpawn callback error" );
	if( std::strcmp( lastContext, "hook.Run(\"PlayerSpawn\")" ) != 0 ) return fail( "PlayerSpawn error context" );

	// --- timer dispatch (production XGLua_RunTimerTick) -------------------
	phase( "timer" );
	if( run( L, "ticks = 0; timer = { Tick = function() ticks = ticks + 1 end }", 100000, dispatchTimer ) != 0 )
		return fail( "timer success path" );
	lua_getglobal( L, "ticks" );
	if( lua_tointeger( L, -1 ) != 100000 ) return fail( "timer tick count" );
	lua_pop( L, 1 );
	if( run( L, "timer = { Tick = function() error( 'expected timer error' ) end }", 10000, dispatchTimer ) != 10000 )
		return fail( "timer execution error count" );
	if( std::strcmp( lastContext, "timer.Tick" ) != 0 ) return fail( "timer error context" );
	if( run( L, "timer = {}", 10000, dispatchTimer ) != 0 ) return fail( "timer Tick missing" );
	if( run( L, "timer = { Tick = 42 }", 10000, dispatchTimer ) != 0 ) return fail( "timer Tick non-function" );
	if( run( L, "timer = nil", 10000, dispatchTimer ) != 0 ) return fail( "timer missing" );
	if( run( L, "timer = 42", 10000, dispatchTimer ) != 0 ) return fail( "timer non-table" );
	if( run( L, "timer = setmetatable( {}, { __index = function( t, k ) if k == 'Tick' then error( 'timer meta' ) end return rawget( t, k ) end } )",
		10000, dispatchTimer ) != 10000 )
		return fail( "timer __index throw not protected" );

	// --- SWEP dispatch: inheritance, errors, missing entries --------------
	phase( "swep" );
	if( run( L, NULL, 100000, dispatchSWEP ) != 0 ) return fail( "SWEP inherited dispatch" );
	lua_rawgeti( L, LUA_REGISTRYINDEX, selfRef );
	lua_getfield( L, -1, "shots" );
	if( lua_tointeger( L, -1 ) != 100000 ) return fail( "SWEP inherited method resolution" );
	lua_pop( L, 2 );

	if( run( L, NULL, 10000, dispatchSWEPMissing ) != 0 ) return fail( "SWEP missing method" );
	if( run( L, NULL, 10000, dispatchSWEPGhostClass ) != 0 ) return fail( "SWEP unknown class" );
	if( run( L, NULL, 10000, dispatchSWEPBrokenClass ) != 0 ) return fail( "SWEP non-table class" );
	if( run( L, NULL, 10000, dispatchSWEPThrowing ) != 10000 ) return fail( "SWEP callback error count" );
	if( std::strcmp( lastContext, "SWEP:Reload" ) != 0 ) return fail( "SWEP error context" );
	if( std::strstr( lastMessage, "expected swep error" ) == NULL ) return fail( "SWEP error message" );
	if( run( L, NULL, 10000, dispatchSWEPThrowingIndex ) != 10000 ) return fail( "SWEP __index throw not protected" );
	if( std::strstr( lastMessage, "swep meta" ) == NULL ) return fail( "SWEP __index error message" );

	// self table must be untouched by the throwing lookups above
	lua_rawgeti( L, LUA_REGISTRYINDEX, selfRef );
	lua_getfield( L, -1, "shots" );
	if( lua_tointeger( L, -1 ) != 100000 ) return fail( "SWEP self table corrupted" );
	lua_pop( L, 2 );

	// --- luaL_loadbuffer protection (lj_load.c lua_loadx via lj_vm_cpcall) -
	phase( "loadbuffer" );
	{
		const int loadBase = lua_gettop( L );
		if( luaL_loadbuffer( L, "return 1 +", 10, "=test" ) != LUA_ERRSYNTAX ) return fail( "syntax status" );
		if( !lua_isstring( L, -1 ) ) return fail( "syntax message missing" );
		lua_pop( L, 1 );
		if( lua_gettop( L ) != loadBase ) return fail( "syntax path stack" );

		if( luaL_loadbuffer( L, "return 40 + 2", 13, "=test" ) != 0 ) return fail( "load status" );
		if( lua_pcall( L, 0, 1, 0 ) != 0 ) return fail( "chunk call" );
		if( lua_tointeger( L, -1 ) != 42 ) return fail( "chunk result" );
		lua_pop( L, 1 );
		if( lua_gettop( L ) != loadBase ) return fail( "load path stack" );
	}

	// --- dispatch hot-path overhead of the lua_cpcall boundary ------------
	phase( "perf" );
	{
		if( run( L, "hook = { Run = function() end }", 1, dispatchThink ) != 0 ) return fail( "perf setup" );
		LARGE_INTEGER freq, t0, t1;
		QueryPerformanceFrequency( &freq );
		QueryPerformanceCounter( &t0 );
		const int iterations = 200000;
		for( int i = 0; i < iterations; ++i )
			dispatchThink( L );
		QueryPerformanceCounter( &t1 );
		double nsPerCall = ( (double)( t1.QuadPart - t0.QuadPart ) * 1e9 ) / ( (double)freq.QuadPart * iterations );
		std::printf( "PERF dispatch overhead: %.0f ns/call (%d iterations)\n", nsPerCall, iterations );
		std::fflush( stdout );
	}

	// --- production ExecuteScript over a memory reader --------------------
	phase( "executescript" );
	{
		static const char chunk[] = "scriptRan = (scriptRan or 0) + 1";
		const int scriptBase = lua_gettop( L );
		if( XGLua_ExecuteScript( L, "ok.lua", MemRead, (void *)chunk, 0 ) != (int)XGLUA_READ_OK )
			return fail( "ExecuteScript success" );
		if( lua_gettop( L ) != scriptBase ) return fail( "ExecuteScript success stack" );
		lua_getglobal( L, "scriptRan" );
		if( lua_tointeger( L, -1 ) != 1 ) return fail( "ExecuteScript side effect" );
		lua_pop( L, 1 );

		if( XGLua_ExecuteScript( L, "missing.lua", MemRead, (void *)chunk, 0 ) != (int)XGLUA_READ_MISSING )
			return fail( "ExecuteScript missing status" );
		if( !lua_isstring( L, -1 ) || !std::strstr( lua_tostring( L, -1 ), "script not found" ) )
			return fail( "ExecuteScript missing message" );
		lua_pop( L, 1 );

		static const char broken[] = "error( 'expected script error' )";
		if( XGLua_ExecuteScript( L, "bad.lua", MemRead, (void *)broken, 0 ) != (int)XGLUA_READ_ERROR )
			return fail( "ExecuteScript runtime error status" );
		if( !lua_isstring( L, -1 ) || !std::strstr( lua_tostring( L, -1 ), "expected script error" ) )
			return fail( "ExecuteScript runtime error message" );
		lua_pop( L, 1 );

		static const char syntax[] = "return +";
		if( XGLua_ExecuteScript( L, "syntax.lua", MemRead, (void *)syntax, 0 ) != (int)XGLUA_READ_ERROR )
			return fail( "ExecuteScript syntax status" );
		if( !lua_isstring( L, -1 ) ) return fail( "ExecuteScript syntax message" );
		lua_pop( L, 1 );

		if( lua_gettop( L ) != scriptBase ) return fail( "ExecuteScript stack across paths" );
	}

	if( !sentinelOk( L, baseline ) ) return fail( "final stack/sentinel" );
	lua_close( L );
	std::puts( "protected call boundary preserves Lua state across all paths" );
	return 0;
}
