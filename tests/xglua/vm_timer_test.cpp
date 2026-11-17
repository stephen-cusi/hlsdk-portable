#include <cstdio>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "xglua_vm_timer.h"

static int errors = 0;
static void onError( lua_State *L, const char *context )
{
    if( std::strcmp( context, "timer.Tick" ) == 0 && lua_tostring( L, -1 ) ) ++errors;
    lua_pop( L, 1 ); // matches the production LuaError callback
}
static int fail( const char *s ) { std::printf( "FAIL: %s\n", s ); return 1; }
static bool run( lua_State *L, const char *setup, int repetitions )
{
    if( luaL_dostring( L, setup ) ) return false;
    const int top = lua_gettop( L );
    for( int i = 0; i < repetitions; ++i )
    {
        XGLua_RunTimerTick( L, onError );
        if( lua_gettop( L ) != top || !lua_isstring( L, 1 ) || std::strcmp( lua_tostring( L, 1 ), "sentinel" ) ) return false;
    }
    return true;
}
int main()
{
    lua_State *L = luaL_newstate();
    if( !L ) return fail( "new state" );
    luaL_openlibs( L );
    lua_pushliteral( L, "sentinel" );
    if( !run( L, "count=0; timer={Tick=function() count=count+1 end}", 100000 ) ) return fail( "success path stack" );
    lua_getglobal( L, "count" );
    if( lua_tointeger( L, -1 ) != 100000 ) return fail( "tick count" );
    lua_pop( L, 1 );
    if( !run( L, "timer={Tick=function() error('expected') end}", 10000 ) ) return fail( "error path stack" );
    if( errors != 10000 ) return fail( "error handler count" );
    if( !run( L, "timer={}", 10000 ) ) return fail( "missing Tick stack" );
    if( !run( L, "timer={Tick=123}", 10000 ) ) return fail( "nonfunction Tick stack" );
    if( !run( L, "timer=nil", 10000 ) ) return fail( "missing timer stack" );
    if( !run( L, "timer=123", 10000 ) ) return fail( "non-table timer stack" );
    if( errors != 10000 ) return fail( "unexpected errors" );
    lua_close( L );
    std::puts( "timer dispatch preserves Lua stack across all paths" );
    return 0;
}
