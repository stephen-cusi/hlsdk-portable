#ifndef XGLUA_SWEP_ORDER_H
#define XGLUA_SWEP_ORDER_H

#include <cstring>

// Push a byte-sorted array of string keys whose values are weapon tables.
// Only Lua stack objects are live while using Lua API calls: an out-of-memory
// longjmp cannot skip a C++ container destructor. No registry metatables run.
static inline int XGLua_PushSortedWeaponNames( lua_State *L, int index )
{
    if( index < 0 ) index = lua_gettop( L ) + index + 1;
    lua_newtable( L );
    const int sorted = lua_gettop( L );
    int count = 0;
    lua_pushnil( L );
    while( lua_next( L, index ) )
    {
        if( lua_type( L, -2 ) == LUA_TSTRING && lua_istable( L, -1 ) )
        {
            size_t keyLength = 0;
            const char *key = lua_tolstring( L, -2, &keyLength );
            int position = count + 1;
            while( position > 1 )
            {
                lua_rawgeti( L, sorted, position - 1 );
                size_t previousLength = 0;
                const char *previous = lua_tolstring( L, -1, &previousLength );
                const size_t common = keyLength < previousLength ? keyLength : previousLength;
                int order = std::memcmp( key, previous, common );
                if( order > 0 || ( order == 0 && keyLength >= previousLength ) )
                {
                    lua_pop( L, 1 );
                    break;
                }
                lua_rawseti( L, sorted, position );
                --position;
            }
            lua_pushvalue( L, -2 );
            lua_rawseti( L, sorted, position );
            ++count;
        }
        lua_pop( L, 1 );
    }
    return count;
}
#endif
