#ifndef XGLUA_WEAPON_ANIM_H
#define XGLUA_WEAPON_ANIM_H

#include <cmath>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

// The engine serializes weapon animation body as a byte.
inline int XGLua_CheckWeaponBody( lua_State *L, int index )
{
    const lua_Number n = luaL_checknumber( L, index );
    if( !std::isfinite( (double)n ) || n < 0 || n > 255 || std::floor( (double)n ) != n )
        return luaL_argerror( L, index, "body must be an integer from 0 to 255" );
    return (int)n;
}

#endif
