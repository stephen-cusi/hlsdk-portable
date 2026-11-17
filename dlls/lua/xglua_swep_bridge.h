#ifndef XGLUA_SWEP_BRIDGE_H
#define XGLUA_SWEP_BRIDGE_H

#include <cstring>

// lua.h must already be included (extern "C"); see xglua_safe.h.
#include "xglua_safe.h"

// lua_cpcall callbacks have a fresh stack frame, so cache code passes a
// temporary registry reference instead of an index into its caller's frame.
static inline void XGLua_PushSWEPTableRef( lua_State *L, int tableRef )
{
    lua_rawgeti( L, LUA_REGISTRYINDEX, tableRef );
}

// Must run inside a protected Lua call because Abstract may be inherited via
// the SWEP table's __index metamethod.
static inline bool XGLua_SWEPIsAbstract( lua_State *L, int tableIndex )
{
    lua_getfield( L, tableIndex, "Abstract" );
    bool result = lua_toboolean( L, -1 ) != 0;
    lua_pop( L, 1 );
    return result;
}

// P1 supports infinite ammunition for clipless weapons only; a finite clip
// requires a real reserve-ammo type for the engine reload implementation.
static inline bool XGLua_ValidAmmoClipConfig( const char *ammo, int clipsize )
{
    if( clipsize < -1 || clipsize == 0 ) return false;
    return !( ammo && std::strcmp( ammo, "none" ) == 0 && clipsize != -1 );
}

static inline int XGLua_InitialWeaponAmmo( bool specified, int amount, int clipsize )
{
    int initial = specified ? amount : clipsize;
    return initial > 0 ? initial : 0;
}

// The caller supplies the SWEP stack index before this function pushes the
// registry.  Normalize it first so adding temporary values cannot change it.
static inline bool XGLua_StoreSWEPTable( lua_State *L, int index, const char *classname )
{
    if( index < 0 ) index = lua_gettop( L ) + index + 1;
    if( !classname || !lua_istable( L, index ) ) return false;
    XGLua_GetSWEPRegistry( L );
    lua_pushstring( L, classname );
    lua_pushvalue( L, index );
    lua_rawset( L, -3 );
    lua_pop( L, 1 );
    return true;
}

// On success leaves only the function on the stack. Inherited methods are
// resolved by the SWEP table's __index metamethod (the Base resolver in
// weapons.lua), which may raise - only safe inside a protected call.
static inline bool XGLua_PushSWEPMethod( lua_State *L, const char *classname, const char *method )
{
    XGLua_GetSWEPRegistry( L );
    lua_pushstring( L, classname );
    lua_rawget( L, -2 );
    if( !lua_istable( L, -1 ) ) { lua_pop( L, 2 ); return false; }
    lua_getfield( L, -1, method );
    if( !lua_isfunction( L, -1 ) ) { lua_pop( L, 3 ); return false; }
    lua_remove( L, -2 );
    lua_remove( L, -2 );
    return true;
}

// Protected SWEP method dispatch for engine callbacks: the inherited lookup
// above, the entity argument allocation, and the call share one lua_pcall
// frame, so neither a throwing __index nor a throwing callback can escape
// into the engine. Stack follows the Lua 5.1 protocol (callee first,
// arguments on top), matching the in-game production dispatch. On error
// onerror receives the error object on top (like LuaError) with context as
// the report prefix; the stack is restored either way.
typedef int ( *XGLua_SWEPArgPush )( lua_State *L, void *ctx );

struct XGLua_SWEPMethodCtx { const char *classname; const char *method; XGLua_SWEPArgPush pushargs; void *argctx; };

static int XGLua_SWEPMethodCore( lua_State *L, void *ctx )
{
    XGLua_SWEPMethodCtx *mc = (XGLua_SWEPMethodCtx *)ctx;
    if( !XGLua_PushSWEPMethod( L, mc->classname, mc->method ) )
        return 0;
    const int nargs = mc->pushargs ? mc->pushargs( L, mc->argctx ) : 0;
    if( lua_pcall( L, nargs, 0, 0 ) != 0 )
        return lua_error( L );      // report through the outer protected frame
    return 0;
}

static inline bool XGLua_CallSWEPMethod( lua_State *L, const char *classname, const char *method,
    XGLua_SWEPArgPush pushargs, void *argctx,
    void ( *onerror )( lua_State *, const char * ), const char *context )
{
    XGLua_SWEPMethodCtx mc = { classname, method, pushargs, argctx };
    return XGLua_ProtectedCall( L, XGLua_SWEPMethodCore, &mc, onerror, context );
}
#endif

