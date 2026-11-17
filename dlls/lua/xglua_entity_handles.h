#ifndef XGLUA_ENTITY_HANDLES_H
#define XGLUA_ENTITY_HANDLES_H

// The per-VM registry owns one canonical userdata per edict index.  Its Lua
// environment is the instance field table, so fields survive collection and
// every acquisition of an entity has the same table-key identity.  A changed
// serial must always replace the record before returning a handle.
#define XGLUA_ENTITY_CACHE "XGLua_EntityHandles"

static XGLuaEntRef *XGLua_TestEntityRef( lua_State *L, int idx )
{
    if( lua_type( L, idx ) != LUA_TUSERDATA || !lua_getmetatable( L, idx ) ) return NULL;
    luaL_getmetatable( L, XGLUA_ENTITY_META );
    bool match = lua_rawequal( L, -1, -2 ) != 0;
    lua_pop( L, 2 );
    return match ? (XGLuaEntRef *)lua_touserdata( L, idx ) : NULL;
}

static void XGLua_PushEntityCache( lua_State *L )
{
    lua_getfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
    if( lua_istable( L, -1 ) ) return;
    lua_pop( L, 1 );
    lua_newtable( L );
    lua_pushvalue( L, -1 );
    lua_setfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
}

// One slot per acquisition amortises clean-up. A slot reused before its turn
// is replaced immediately. No scan ever traverses a Lua table under mutation.
static void XGLua_SweepEntitySlot( lua_State *L, int cache, int index )
{
    lua_rawgeti( L, cache, index );
    XGLuaEntRef *ref = XGLua_TestEntityRef( L, -1 );
    bool stale = ref && !XGLua_ValidateEnt( ref );
    lua_pop( L, 1 );
    if( stale ) { lua_pushnil( L ); lua_rawseti( L, cache, index ); }
}

// Run a fixed amount of clean-up even if scripts stop fetching entities.
// The registry is per-VM; the cursor is normalized to the active slot range.
static void XGLua_SweepEntityHandles( lua_State *L, int maxSlots, int budget, int *cursor )
{
    if( !L || !cursor || maxSlots <= 0 || budget <= 0 ) return;
    lua_getfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
    if( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); return; }
    int cache = lua_gettop( L );
    if( *cursor < 0 || *cursor >= maxSlots ) *cursor = 0;
    if( budget > maxSlots ) budget = maxSlots;
    for( int i = 0; i < budget; ++i )
    {
        XGLua_SweepEntitySlot( L, cache, *cursor );
        *cursor = ( *cursor + 1 ) % maxSlots;
    }
    lua_pop( L, 1 );
}

static void XGLua_PushEntityHandle( lua_State *L, edict_t *pent, int index, int sweepIndex )
{
    XGLua_PushEntityCache( L );
    int cache = lua_gettop( L );
    if( sweepIndex >= 0 ) XGLua_SweepEntitySlot( L, cache, sweepIndex );
    lua_rawgeti( L, cache, index );
    XGLuaEntRef *ref = XGLua_TestEntityRef( L, -1 );
    if( ref && ref->pent == pent && ref->serial == pent->serialnumber && XGLua_ValidateEnt( ref ) )
    {
        lua_remove( L, cache );
        return;
    }
    lua_pop( L, 1 );
    ref = (XGLuaEntRef *)lua_newuserdata( L, sizeof( XGLuaEntRef ) );
    ref->pent = pent;
    ref->serial = pent->serialnumber;
    luaL_getmetatable( L, XGLUA_ENTITY_META );
    lua_setmetatable( L, -2 );
    lua_newtable( L );
    lua_setfenv( L, -2 );
    lua_pushvalue( L, -1 );
    lua_rawseti( L, cache, index );
    lua_remove( L, cache );
}

// Leaves the value on the stack, including nil. Raw access prevents user
// metatables from changing field lookup or raising unprotected errors.
static void XGLua_GetEntityField( lua_State *L, int entity, int key )
{
    if( entity < 0 ) entity += lua_gettop( L ) + 1;
    if( key < 0 ) key += lua_gettop( L ) + 1;
    lua_getfenv( L, entity );
    lua_pushvalue( L, key );
    lua_rawget( L, -2 );
    lua_remove( L, -2 );
}

static void XGLua_SetEntityField( lua_State *L, int entity, int key, int value )
{
    if( entity < 0 ) entity += lua_gettop( L ) + 1;
    if( key < 0 ) key += lua_gettop( L ) + 1;
    if( value < 0 ) value += lua_gettop( L ) + 1;
    lua_getfenv( L, entity );
    lua_pushvalue( L, key );
    lua_pushvalue( L, value );
    lua_rawset( L, -3 );
    lua_pop( L, 1 );
}

#endif
