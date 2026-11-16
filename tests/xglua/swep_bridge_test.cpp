#include <cstdio>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
static int registryRef;
void XGLua_GetSWEPRegistry( lua_State *L ) { lua_rawgeti( L, LUA_REGISTRYINDEX, registryRef ); }
#include "xglua_swep_bridge.h"
static int fail( const char *msg ) { std::printf("FAIL: %s\n", msg ); return 1; }
int main() {
    if( XGLua_ValidAmmoClipConfig( "none", 6 ) || XGLua_ValidAmmoClipConfig( "grenade", -2 ) || XGLua_ValidAmmoClipConfig( "grenade", 0 ) || !XGLua_ValidAmmoClipConfig( "none", -1 ) || !XGLua_ValidAmmoClipConfig( "grenade", 6 ) ) return fail("ammo/clip validation");
    if( XGLua_InitialWeaponAmmo( true, 0, 6 ) != 0 || XGLua_InitialWeaponAmmo( false, 0, -1 ) != 0 || XGLua_InitialWeaponAmmo( false, 0, 6 ) != 6 || XGLua_InitialWeaponAmmo( true, 18, 6 ) != 18 ) return fail("initial ammo");
    lua_State *L = luaL_newstate(); luaL_openlibs( L );
    lua_newtable( L ); registryRef = luaL_ref( L, LUA_REGISTRYINDEX );
    // Match lua_next's live table, key and value, then check exact identity.
    if( luaL_dostring( L, "local base={PrimaryAttack=function(self) self.shots=self.shots+1 end}; return {weapon_grenadepistol=setmetatable({}, {__index=base})}" ) ) return fail("test setup");
    lua_pushnil( L ); if( !lua_next( L, -2 ) ) return fail("lua_next");
    const void *table = lua_topointer( L, -1 );
    if( !XGLua_StoreSWEPTable( L, -1, lua_tostring( L, -2 ) ) ) return fail("store");
    if( lua_gettop( L ) != 3 ) return fail("store stack balance");
    XGLua_GetSWEPRegistry( L ); lua_getfield( L, -1, "weapon_grenadepistol" );
    if( !lua_istable( L, -1 ) || lua_topointer( L, -1 ) != table ) return fail("registered wrong value");
    lua_pop( L, 2 );
    if( !XGLua_PushSWEPMethod( L, "weapon_grenadepistol", "PrimaryAttack" ) ) return fail("inherited method lookup");
    lua_newtable( L ); lua_pushnumber( L, 0 ); lua_setfield( L, -2, "shots" );
    lua_pushvalue( L, -1 ); int self = luaL_ref( L, LUA_REGISTRYINDEX );
    if( lua_pcall( L, 1, 0, 0 ) ) return fail( lua_tostring( L, -1 ) );
    lua_rawgeti( L, LUA_REGISTRYINDEX, self ); lua_getfield( L, -1, "shots" );
    if( lua_tointeger( L, -1 ) != 1 ) return fail("attack callback did not execute");
    lua_pop( L, 2 );
    if( XGLua_PushSWEPMethod( L, "weapon_grenadepistol", "missing" ) || lua_gettop( L ) != 3 ) return fail("missing method stack balance");
    lua_pushstring( L, "not a table" );
    if( XGLua_StoreSWEPTable( L, -1, "bad" ) ) return fail("invalid registration accepted");
    lua_pop( L, 1 ); lua_close( L ); std::puts("real Lua C API registration and inherited attack callback OK");
    return 0;
}
