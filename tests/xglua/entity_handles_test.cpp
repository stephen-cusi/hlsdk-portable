#include <cstdio>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
struct edict_t { bool free; int serialnumber; const char *classname; };
struct XGLuaEntRef { edict_t *pent; int serial; };
#define XGLUA_ENTITY_META "XGLua_Entity"
static edict_t *XGLua_ValidateEnt( XGLuaEntRef *r ) {
    return r && r->pent && !r->pent->free && r->pent->serialnumber == r->serial ? r->pent : NULL;
}
#include "xglua_entity_handles.h"
static edict_t slots[3];
static int cursor;
static int entity( lua_State *L ) {
    int i = (int)luaL_checkinteger(L, 1);
    if(i < 0 || i >= 3) return luaL_error(L,"bad index");
    if( slots[i].free ) { lua_pushnil(L); return 1; }
    XGLua_PushEntityHandle(L, &slots[i], i, cursor++ % 3); return 1;
}
static int valid( lua_State *L ) { XGLuaEntRef *r = XGLua_TestEntityRef(L, 1); lua_pushboolean(L, r && XGLua_ValidateEnt(r)); return 1; }
static int Ent_IsValid( lua_State *L ) { return valid(L); }
static edict_t *XGLua_CheckEntity( lua_State *L, int idx ) {
    XGLuaEntRef *r=XGLua_TestEntityRef(L,idx);
    edict_t *p=XGLua_ValidateEnt(r);
    if(!p) luaL_error(L,"Tried to use a NULL entity!");
    return p;
}
static const char *XGLua_EntityClassName( edict_t *p ) { return p->classname; }
static bool XGLua_IsLuaWeapon( const char *c ) { return c && !std::strcmp(c,"weapon_test"); }
static void XGLua_GetSWEPRegistry( lua_State *L ) { lua_getfield(L,LUA_REGISTRYINDEX,"TestSWEP"); }
#include "xglua_entity_methods.h"
static int nativeMethod( lua_State *L ) { lua_pushinteger(L,17); return 1; }
static int frame( lua_State *L ) { int before=lua_gettop(L); XGLua_SweepEntityHandles(L,3,8,&cursor); if(lua_gettop(L)!=before) return luaL_error(L,"sweep stack imbalance"); return 0; }
static int invalidate( lua_State *L ) { slots[luaL_checkinteger(L,1)].free=true; return 0; }
static int reuse( lua_State *L ) { int i=(int)luaL_checkinteger(L,1); slots[i].serialnumber++; slots[i].free=false; return 0; }
static void bind(lua_State *L, const char *name, lua_CFunction f) { lua_pushcfunction(L,f); lua_setglobal(L,name); }
int main() {
    lua_State *L=luaL_newstate(); luaL_openlibs(L);
    slots[1].classname="weapon_test"; slots[2].classname="native";
    XGLua_SweepEntityHandles(NULL,3,8,&cursor);
    int before=lua_gettop(L); XGLua_SweepEntityHandles(L,3,8,&cursor);
    if(lua_gettop(L)!=before) return 2;
    luaL_newmetatable(L,XGLUA_ENTITY_META);
    lua_pushcfunction(L,Ent_Index); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,Ent_NewIndex); lua_setfield(L,-2,"__newindex");
    lua_pushcfunction(L,valid); lua_setfield(L,-2,"IsValid");
    lua_pushcfunction(L,nativeMethod); lua_setfield(L,-2,"NativeMethod"); lua_pop(L,1);
    if(luaL_dostring(L,"return {weapon_test=setmetatable({ClassValue='class'}, {__index={InheritedValue='base'}})}")) return 3;
    lua_setfield(L,LUA_REGISTRYINDEX,"TestSWEP");
    bind(L,"Entity",entity); bind(L,"IsValid",valid); bind(L,"invalidate",invalidate); bind(L,"reuse",reuse); bind(L,"frame",frame);
    const char *test=R"(
      local world=Entity(0); assert(IsValid(world) and world==Entity(0))
      local a=Entity(1); local b=Entity(1); local c=Entity(2)
      assert(a.ClassValue=='class' and a.InheritedValue=='base' and a:NativeMethod()==17)
      assert(c.ClassValue==nil and c:NativeMethod()==17)
      a.ClassValue='instance'; assert(b.ClassValue=='instance'); a.ClassValue=nil; assert(a.ClassValue=='class')
      a.NativeMethod=false; assert(b.NativeMethod==false); a.NativeMethod=nil; assert(a:NativeMethod()==17)
      assert(a==b and a~=c); local map={[a]='ok'}; assert(map[b]=='ok')
      a.value={n=42}; assert(b.value.n==42 and c.value==nil)
      a,b=nil,nil; collectgarbage(); collectgarbage(); assert(Entity(1).value.n==42)
      assert(not IsValid(nil) and not IsValid(false) and not IsValid(123) and not IsValid({}) and not IsValid(io.stdout))
      local old=Entity(1); invalidate(1); assert(not IsValid(old) and not old:IsValid())
      assert(not pcall(function() return old.value end)); assert(not pcall(function() old.foo=1 end))
      reuse(1); local fresh=Entity(1); assert(fresh~=old and fresh.value==nil and IsValid(fresh))
      fresh.value='fresh'; assert(Entity(1).value=='fresh')
      -- Old records not explicitly removed are swept on subsequent pushes.
      local weak=setmetatable({}, {__mode='v'}); local doomed=Entity(2)
      local payload={}; weak[1]=payload; doomed.payload=payload; doomed,payload,c=nil,nil,nil
      invalidate(2); frame(); collectgarbage(); collectgarbage(); assert(weak[1]==nil)
      assert(Entity(1).value=='fresh')
    )";
    int status=luaL_dostring(L,test);
    if(status) std::printf("FAIL: %s\n",lua_tostring(L,-1)); else std::puts("entity identity, fields, serial reuse, invalid access and cleanup OK");
    lua_close(L);
    // A fresh VM starts with no prior cache or instance fields.
    L=luaL_newstate(); before=lua_gettop(L); XGLua_SweepEntityHandles(L,3,8,&cursor);
    lua_getfield(L,LUA_REGISTRYINDEX,XGLUA_ENTITY_CACHE);
    bool clean=lua_isnil(L,-1); lua_pop(L,1); clean=clean && lua_gettop(L)==before; lua_close(L);
    return status || !clean ? 1 : 0;
}
