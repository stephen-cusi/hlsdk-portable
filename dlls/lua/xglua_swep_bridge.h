#ifndef XGLUA_SWEP_BRIDGE_H
#define XGLUA_SWEP_BRIDGE_H

#include <cstring>

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
// resolved by the SWEP table's __index metamethod.
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
#endif

