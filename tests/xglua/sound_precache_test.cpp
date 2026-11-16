#include <cstdio>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "xglua_sound_precache.h"
static int fail( const char *msg ) { std::printf("FAIL: %s\n", msg ); return 1; }
static bool fileExists( const char *name ) { return std::strcmp( name, "weapons/missing.wav" ) != 0; }
static bool load( lua_State *L, const char *code ) { return luaL_dostring( L, code ) == 0; }
int main() {
 lua_State *L=luaL_newstate(); luaL_openlibs(L);
 char paths[XGLUA_SOUND_PER_WEAPON][XGLUA_SOUND_PATH_BYTES] = {};
 char global[XGLUA_SOUND_TOTAL][XGLUA_SOUND_PATH_BYTES] = {};
 int count=0;
 if(!load(L,"return setmetatable({}, {__index={PrecacheSounds={'weapons/hks1.wav','weapons/glauncher.wav'}}})")) return fail("setup");
 int top=lua_gettop(L);
 if(!XGLua_ReadPrecacheSounds(L,-1,paths,&count)||count!=2||lua_gettop(L)!=top||std::strcmp(paths[0],"weapons/hks1.wav")) return fail("inherited and stack");
 if(!XGLua_ValidateSoundResources(paths,count,global,0,fileExists)) return fail("existing samples");
 lua_pop(L,1);
 const char* invalid[]={"return {PrecacheSounds='bad'}", "return {PrecacheSounds={[2]='x.wav'}}", "return {PrecacheSounds={a='x.wav'}}", "return {PrecacheSounds={42}}", "return {PrecacheSounds={'../evil.wav'}}", "return {PrecacheSounds={'/absolute.wav'}}", "return {PrecacheSounds={'folder//x.wav'}}", "return {PrecacheSounds={'test.mp3'}}", "return {PrecacheSounds={'x\\0y.wav'}}", "return {PrecacheSounds={[33]='x.wav'}}"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) { if(!load(L,invalid[i])) return fail("invalid setup"); int n=lua_gettop(L); if(XGLua_ReadPrecacheSounds(L,-1,paths,&count)||lua_gettop(L)!=n) return fail("accepted malformed list or leaked stack"); lua_pop(L,1); }
 if(!load(L,"return {PrecacheSounds={'weapons/hks1.wav','weapons/missing.wav'}}")) return fail("missing setup");
 if(!XGLua_ReadPrecacheSounds(L,-1,paths,&count)||XGLua_ValidateSoundResources(paths,count,global,0,fileExists)) return fail("missing file"); lua_pop(L,1);
 if(!load(L,"return {PrecacheSounds={'weapons/hks1.wav','WEAPONS/HKS1.WAV'}}")) return fail("duplicate setup");
 if(!XGLua_ReadPrecacheSounds(L,-1,paths,&count)) return fail("duplicate parse");
 for(int i=0;i<63;++i) std::snprintf(global[i],sizeof(global[i]),"existing/%d.wav",i);
 if(!XGLua_ValidateSoundResources(paths,count,global,63,fileExists)) return fail("intra-SWEP dedup");
 std::strcpy(global[63],"weapons/hks1.wav");
 if(!XGLua_ValidateSoundResources(paths,count,global,64,fileExists)) return fail("cross-SWEP dedup");
 std::strcpy(global[63],"different.wav");
 if(XGLua_ValidateSoundResources(paths,count,global,64,fileExists)) return fail("global budget");
 lua_close(L); std::puts("Lua sound declarations, inheritance, paths, VFS checks, dedup and limits OK");
}
