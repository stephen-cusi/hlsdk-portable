// Shared host-test framework for the XGLua regression suite.
//
// Contract (see doc report): every test case is registered by name with
// TEST(name), gets a FRESH lua_State plus a rebuilt fixture (the per-file
// Setup function), and uses CHECK / CHECK_EQ_* / CheckLua* assertions that
// accumulate failures instead of aborting. Each failure line carries the
// case name, a description naming the API under test, and expected/actual
// values. The runner prints one PASS/FAIL line per case and exits nonzero
// if any check failed.
//
// Lua safety rule: Lua errors are longjmps. Case bodies must only run Lua
// through the protected helpers here (LUA / CheckLua* / ExpectLuaError) or
// their own lua_pcall — never through unprotected luaL_dostring.
//
// The runner pushes a stack sentinel after setup and verifies the stack
// top and sentinel after every case ("stack balance" check).

#ifndef XGLUA_TEST_FRAMEWORK_H
#define XGLUA_TEST_FRAMEWORK_H

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace xtest {

typedef void (*CaseFn)( lua_State *L );
typedef bool (*SetupFn)( lua_State *L );

struct Case { const char *name; CaseFn fn; };

inline Case *Cases() { static Case cases[512]; return cases; }
inline int &CaseCount() { static int count = 0; return count; }
inline SetupFn &Setup() { static SetupFn fn = NULL; return fn; }
inline int &Checks() { static int n = 0; return n; }
inline int &Failures() { static int n = 0; return n; }
inline const char *&CurrentCase() { static const char *name = "?"; return name; }

struct Registrar
{
	Registrar( const char *name, CaseFn fn )
	{
		int n = CaseCount();
		if( n < 512 ) { Cases()[n].name = name; Cases()[n].fn = fn; CaseCount() = n + 1; }
	}
};

inline void Fail( const char *desc, const char *file, int line )
{
	++Failures();
	std::printf( "  FAIL [%s] %s (%s:%d)\n", CurrentCase(), desc, file, line );
	std::fflush( stdout );
}

inline bool Check( bool cond, const char *desc, const char *file, int line )
{
	++Checks();
	if( !cond ) Fail( desc, file, line );
	return cond;
}

inline bool CheckInt( long long actual, long long expected, const char *desc, const char *file, int line )
{
	++Checks();
	if( actual != expected )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected %lld, got %lld (%s:%d)\n",
			CurrentCase(), desc, expected, actual, file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

inline bool CheckBool( bool actual, bool expected, const char *desc, const char *file, int line )
{
	++Checks();
	if( actual != expected )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected %s, got %s (%s:%d)\n",
			CurrentCase(), desc, expected ? "true" : "false", actual ? "true" : "false", file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

inline bool CheckStr( const char *actual, const char *expected, const char *desc, const char *file, int line )
{
	++Checks();
	bool ok = actual && expected && std::strcmp( actual, expected ) == 0;
	if( !ok )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected \"%s\", got \"%s\" (%s:%d)\n",
			CurrentCase(), desc, expected ? expected : "(null)", actual ? actual : "(null)", file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

inline bool CheckFlt( double actual, double expected, double eps, const char *desc, const char *file, int line )
{
	++Checks();
	if( !( std::fabs( actual - expected ) <= eps ) )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected %g, got %g (%s:%d)\n",
			CurrentCase(), desc, expected, actual, file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

inline bool CheckPtr( const void *actual, const void *expected, const char *desc, const char *file, int line )
{
	++Checks();
	if( actual != expected )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected pointer %p, got %p (%s:%d)\n",
			CurrentCase(), desc, expected, actual, file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------
// protected Lua helpers
//---------------------------------------------------------------------------

// Runs a statement chunk under pcall; on error records a failure with the
// Lua message. Returns false on error (the LUA macro then leaves the case).
inline bool RunLua( lua_State *L, const char *code, const char *file, int line )
{
	if( luaL_loadstring( L, code ) == 0 && lua_pcall( L, 0, 0, 0 ) == 0 )
		return true;
	++Checks();
	++Failures();
	const char *msg = lua_tostring( L, -1 );
	std::printf( "  FAIL [%s] Lua fixture error: %s (%s:%d)\n",
		CurrentCase(), msg ? msg : luaL_typename( L, -1 ), file, line );
	std::fflush( stdout );
	lua_pop( L, 1 );
	return false;
}

// Evaluates "return <expr>" under pcall, leaving one value on success.
// On error records a failure (with desc) and leaves the stack unchanged.
inline bool Eval( lua_State *L, const char *expr, const char *desc, const char *file, int line )
{
	lua_pushfstring( L, "return %s", expr );
	int status = luaL_loadstring( L, lua_tostring( L, -1 ) );
	lua_remove( L, -2 );	// drop the code string, keep the chunk (or error)
	if( status == 0 )
		status = lua_pcall( L, 0, 1, 0 );
	if( status != 0 )
	{
		++Checks();
		++Failures();
		const char *msg = lua_tostring( L, -1 );
		std::printf( "  FAIL [%s] %s: evaluating '%s' raised: %s (%s:%d)\n",
			CurrentCase(), desc, expr, msg ? msg : luaL_typename( L, -1 ), file, line );
		std::fflush( stdout );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

inline bool CheckLuaInt( lua_State *L, const char *expr, long long expected, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	long long actual = (long long)lua_tointeger( L, -1 );
	lua_pop( L, 1 );
	return CheckInt( actual, expected, desc, file, line );
}

inline bool CheckLuaFlt( lua_State *L, const char *expr, double expected, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	double actual = lua_tonumber( L, -1 );
	lua_pop( L, 1 );
	return CheckFlt( actual, expected, 1e-4, desc, file, line );
}

inline bool CheckLuaStr( lua_State *L, const char *expr, const char *expected, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	const char *actual = lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : NULL;
	bool ok = CheckStr( actual, expected, desc, file, line );
	lua_pop( L, 1 );
	return ok;
}

inline bool CheckLuaBool( lua_State *L, const char *expr, bool expected, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	bool actual = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );
	return CheckBool( actual, expected, desc, file, line );
}

inline bool CheckLuaNil( lua_State *L, const char *expr, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	int type = lua_type( L, -1 );
	lua_pop( L, 1 );
	++Checks();
	if( type != LUA_TNIL )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected nil, got %s (%s:%d)\n",
			CurrentCase(), desc, lua_typename( L, type ), file, line );
		std::fflush( stdout );
		return false;
	}
	return true;
}

inline bool CheckLuaType( lua_State *L, const char *expr, const char *expected, const char *desc, const char *file, int line )
{
	if( !Eval( L, expr, desc, file, line ) ) return false;
	const char *actual = luaL_typename( L, -1 );
	bool ok = CheckStr( actual, expected, desc, file, line );
	lua_pop( L, 1 );
	return ok;
}

// Raw identity of two expressions (rawequal: no __eq metamethod).
inline bool CheckLuaSame( lua_State *L, const char *exprA, const char *exprB, const char *desc, const char *file, int line )
{
	int top = lua_gettop( L );
	if( !Eval( L, exprA, desc, file, line ) ) return false;
	if( !Eval( L, exprB, desc, file, line ) ) { lua_settop( L, top ); return false; }
	bool same = lua_rawequal( L, -1, -2 ) != 0;
	lua_settop( L, top );
	return Check( same, desc, file, line );
}

// Expects the statement chunk to raise a Lua error whose message contains
// needle (plain substring). Reports expected needle vs actual outcome.
inline bool ExpectLuaError( lua_State *L, const char *code, const char *needle, const char *desc, const char *file, int line )
{
	++Checks();
	int top = lua_gettop( L );
	int status = luaL_loadstring( L, code );
	if( status == 0 )
		status = lua_pcall( L, 0, 0, 0 );
	if( status == 0 )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected error containing \"%s\", but the code succeeded (%s:%d)\n",
			CurrentCase(), desc, needle, file, line );
		std::fflush( stdout );
		lua_settop( L, top );
		return false;
	}
	const char *msg = lua_tostring( L, -1 );
	bool ok = msg && std::strstr( msg, needle ) != NULL;
	if( !ok )
	{
		++Failures();
		std::printf( "  FAIL [%s] %s: expected error containing \"%s\", got \"%s\" (%s:%d)\n",
			CurrentCase(), desc, needle, msg ? msg : luaL_typename( L, -1 ), file, line );
		std::fflush( stdout );
	}
	lua_settop( L, top );
	return ok;
}

//---------------------------------------------------------------------------
// runner
//---------------------------------------------------------------------------

inline DWORD WINAPI Watchdog( LPVOID )
{
	Sleep( 300000 );
	std::printf( "WATCHDOG: suite timeout after 300s\n" );
	std::fflush( stdout );
	_exit( 2 );
	return 0;
}

inline int RunAll( const char *suite )
{
	std::printf( "SUITE %s: %d cases\n", suite, CaseCount() );
	std::fflush( stdout );
	CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );

	int failedCases = 0, totalChecks = 0, totalFailures = 0;
	for( int i = 0; i < CaseCount(); ++i )
	{
		CurrentCase() = Cases()[i].name;
		Checks() = 0;
		Failures() = 0;

		lua_State *L = luaL_newstate();
		if( !L )
		{
			std::printf( "FAIL %s (luaL_newstate returned NULL)\n", CurrentCase() );
			++failedCases;
			continue;
		}
		luaL_openlibs( L );
		bool setupOk = !Setup() || Setup()( L );
		lua_pushliteral( L, "xglua-stack-sentinel" );
		const int baseline = lua_gettop( L );
		if( setupOk )
			Cases()[i].fn( L );
		else
			Fail( "fixture setup failed", __FILE__, __LINE__ );

		++Checks();
		int top = lua_gettop( L );
		if( top != baseline || !lua_isstring( L, baseline ) ||
			std::strcmp( lua_tostring( L, baseline ), "xglua-stack-sentinel" ) != 0 )
		{
			++Failures();
			std::printf( "  FAIL [%s] stack balance: expected top %d with sentinel, got top %d\n",
				CurrentCase(), baseline, top );
			std::fflush( stdout );
		}
		lua_close( L );

		totalChecks += Checks();
		totalFailures += Failures();
		if( Failures() == 0 )
			std::printf( "PASS %s (%d checks)\n", CurrentCase(), Checks() );
		else
		{
			++failedCases;
			std::printf( "FAIL %s (%d of %d checks failed)\n", CurrentCase(), Failures(), Checks() );
		}
		std::fflush( stdout );
	}
	std::printf( "SUMMARY %s: %d cases (%d failed), %d checks (%d failed)\n",
		suite, CaseCount(), failedCases, totalChecks, totalFailures );
	std::fflush( stdout );
	return ( failedCases == 0 && totalFailures == 0 ) ? 0 : 1;
}

} // namespace xtest

// Registers a named case; the body receives the fresh, fixture-ready state.
#define TEST( name ) \
	static void name( lua_State *L ); \
	static ::xtest::Registrar xtest_reg_##name( #name, name ); \
	static void name( lua_State *L )

#define CHECK( cond, desc ) ::xtest::Check( !!( cond ), desc, __FILE__, __LINE__ )
#define CHECK_EQ_INT( a, e, desc ) ::xtest::CheckInt( (long long)( a ), (long long)( e ), desc, __FILE__, __LINE__ )
#define CHECK_EQ_BOOL( a, e, desc ) ::xtest::CheckBool( !!( a ), !!( e ), desc, __FILE__, __LINE__ )
#define CHECK_EQ_STR( a, e, desc ) ::xtest::CheckStr( ( a ), ( e ), desc, __FILE__, __LINE__ )
#define CHECK_EQ_FLT( a, e, desc ) ::xtest::CheckFlt( ( a ), ( e ), 1e-4, desc, __FILE__, __LINE__ )
#define CHECK_EQ_PTR( a, e, desc ) ::xtest::CheckPtr( ( a ), ( e ), desc, __FILE__, __LINE__ )

// Fixture statement: on Lua error the failure is recorded and the case ends.
#define LUA( code ) do { if( !::xtest::RunLua( L, code, __FILE__, __LINE__ ) ) return; } while( 0 )

#define CHECK_LUA_INT( expr, e, desc ) ::xtest::CheckLuaInt( L, expr, (long long)( e ), desc, __FILE__, __LINE__ )
#define CHECK_LUA_FLT( expr, e, desc ) ::xtest::CheckLuaFlt( L, expr, ( e ), desc, __FILE__, __LINE__ )
#define CHECK_LUA_STR( expr, e, desc ) ::xtest::CheckLuaStr( L, expr, ( e ), desc, __FILE__, __LINE__ )
#define CHECK_LUA_BOOL( expr, e, desc ) ::xtest::CheckLuaBool( L, expr, !!( e ), desc, __FILE__, __LINE__ )
#define CHECK_LUA_NIL( expr, desc ) ::xtest::CheckLuaNil( L, expr, desc, __FILE__, __LINE__ )
#define CHECK_LUA_TYPE( expr, e, desc ) ::xtest::CheckLuaType( L, expr, ( e ), desc, __FILE__, __LINE__ )
#define CHECK_LUA_SAME( a, b, desc ) ::xtest::CheckLuaSame( L, a, b, desc, __FILE__, __LINE__ )
#define CHECK_LUA_ERROR( code, needle, desc ) ::xtest::ExpectLuaError( L, code, needle, desc, __FILE__, __LINE__ )

#endif // XGLUA_TEST_FRAMEWORK_H