#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "xglua_addon_load.h"
static int errors = 0;
static void OnError( lua_State *L, const char * ) { ++errors; lua_pop(L, 1); }
static XGLuaReadStatus Read( void *ctx, const char *rel, char **out, size_t *len )
{
    char path[1024];
    if( std::snprintf(path, sizeof(path), "%s/%s", (const char *)ctx, rel) >= (int)sizeof(path) ) return XGLUA_READ_ERROR;
    FILE *f = std::fopen(path, "rb"); if(!f) return XGLUA_READ_MISSING;
    if(std::fseek(f, 0, SEEK_END)) { std::fclose(f); return XGLUA_READ_ERROR; }
    long n=std::ftell(f); if(n < 0 || std::fseek(f, 0, SEEK_SET)) { std::fclose(f); return XGLUA_READ_ERROR; }
    char *buf=(char *)std::malloc((size_t)n+1); if(!buf) { std::fclose(f); return XGLUA_READ_ERROR; }
    size_t got=std::fread(buf, 1, (size_t)n, f); std::fclose(f);
    if(got != (size_t)n) { std::free(buf); return XGLUA_READ_ERROR; }
    buf[n]=0; *out=buf; *len=(size_t)n; return XGLUA_READ_OK;
}
int main(int argc, char **argv)
{
    if(argc != 2) return 2;
    lua_State *L=luaL_newstate(); if(!L) return 2; luaL_openlibs(L);
    if(luaL_dostring(L, "weapons = {}; function weapons.Register(t,n) weapons[n]=t end")) return 2;
    lua_pushliteral(L, "sentinel");
    XGLua_LoadAddonWeapons(L, argv[1], Read, argv[1], OnError);
    if(errors != 1 || lua_gettop(L) != 1 || std::strcmp(lua_tostring(L,1), "sentinel")) return 1;
    if(luaL_dostring(L, "assert(weapons.good and weapons.good.Mark == 42); assert(not weapons.bad)")) {
        std::puts(lua_tostring(L,-1)); return 1;
    }
    lua_close(L);
    std::puts("addon execution: protected failure, continuation and stack OK");
    return 0;
}
