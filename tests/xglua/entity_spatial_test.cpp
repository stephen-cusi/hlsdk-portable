#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
struct Vector { float x,y,z; Vector(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
struct edict_t;
struct entvars_t { Vector origin,mins,maxs,size; float max_health; edict_t *groundentity; const char *classname; };
struct edict_t { bool free; int serialnumber; void *pvPrivateData; entvars_t v; };
struct Globals { int maxEntities; } globals={4}, *gpGlobals=&globals;
static edict_t slots[4];
static int spawnCalls,removeCalls;
struct CBaseEntity { static CBaseEntity *Instance(edict_t *e) {return (CBaseEntity*)e->pvPrivateData;} void SUB_Remove(){++removeCalls;} };
static CBaseEntity objects[4];
static void DispatchSpawn(edict_t *) {++spawnCalls;}
struct XGLuaEntRef { edict_t *pent; int serial; };
#define XGLUA_ENTITY_META "XGLua_Entity"
#define VARS(e) (&(e)->v)
#define STRING(s) (s)
static edict_t *XGLua_ValidateEnt(XGLuaEntRef *r) { return r && r->pent && !r->pent->free && r->pent->serialnumber==r->serial ? r->pent : NULL; }
#include "xglua_entity_handles.h"
static int indexCalls;
static edict_t *INDEXENT(int i) { ++indexCalls; return i>=0 && i<4 ? &slots[i] : NULL; }
static int ENTINDEX(edict_t *e) { return (int)(e-slots); }
static edict_t *XGLua_CheckEntity(lua_State *L,int idx) { XGLuaEntRef *r=XGLua_TestEntityRef(L,idx); edict_t *e=XGLua_ValidateEnt(r); if(!e) luaL_error(L,"Tried to use a NULL entity!"); return e; }
static Vector XGLua_CheckVector(lua_State *L,int idx) { luaL_checktype(L,idx,LUA_TTABLE); float v[3]; for(int i=1;i<=3;i++){lua_rawgeti(L,idx,i);v[i-1]=(float)luaL_checknumber(L,-1);lua_pop(L,1);} return Vector(v[0],v[1],v[2]); }
static int originCalls,sizeCalls; static edict_t *lastOrigin,*lastSize;
static void UTIL_SetOrigin(entvars_t *v,const Vector &pos) { ++originCalls; lastOrigin=(edict_t*)((char*)v - offsetof(edict_t,v)); v->origin=pos; }
static void UTIL_SetSize(entvars_t *v,const Vector &mins,const Vector &maxs) { ++sizeCalls;lastSize=(edict_t*)((char*)v - offsetof(edict_t,v));v->mins=mins;v->maxs=maxs;v->size=Vector(maxs.x-mins.x,maxs.y-mins.y,maxs.z-mins.z); }
#include "xglua_entity_spatial.h"
static int valid(lua_State *L) {XGLuaEntRef *r=XGLua_TestEntityRef(L,1);lua_pushboolean(L,r && XGLua_ValidateEnt(r));return 1;}
static int idx(lua_State *L) {lua_pushinteger(L,ENTINDEX(XGLua_CheckEntity(L,1)));return 1;}
static int nullEntity(lua_State *L) { XGLua_PushEntity(L,NULL);return 1; }
static int invalidate(lua_State *L) { slots[(int)luaL_checkinteger(L,1)].free=true;return 0; }
static int reuse(lua_State *L) {int i=(int)luaL_checkinteger(L,1);++slots[i].serialnumber;slots[i].free=false;return 0;}
static int worldPrivate(lua_State *L) {slots[0].pvPrivateData=lua_toboolean(L,1)?&objects[0]:NULL;return 0;}
static int lifecycleCounts(lua_State *L) {lua_pushinteger(L,spawnCalls);lua_pushinteger(L,removeCalls);return 2;}
static int counts(lua_State *L) { lua_pushinteger(L,originCalls);lua_pushinteger(L,sizeCalls);return 2; }
static int spatial(lua_State *L) { int n=(int)luaL_checkinteger(L,1); const entvars_t &v=slots[n].v;lua_pushnumber(L,v.origin.x);lua_pushnumber(L,v.origin.y);lua_pushnumber(L,v.origin.z);lua_pushnumber(L,v.mins.x);lua_pushnumber(L,v.maxs.y);lua_pushnumber(L,v.size.z);lua_pushboolean(L,lastOrigin==&slots[n]);lua_pushboolean(L,lastSize==&slots[n]);return 8; }
static int lookupCalls(lua_State *L) {lua_pushinteger(L,indexCalls);return 1;}
static void bind(lua_State *L,const char *name,lua_CFunction f){lua_pushcfunction(L,f);lua_setglobal(L,name);}
static void method(lua_State *L,const char *name,lua_CFunction f){lua_pushcfunction(L,f);lua_setfield(L,-2,name);}
int main(){
  lua_State *L=luaL_newstate();luaL_openlibs(L);
  for(int i=0;i<4;i++){slots[i].pvPrivateData=&objects[i];slots[i].v.classname=i==0?"worldspawn":i==1?"player":"item";}
  slots[3].free=true; slots[1].v.groundentity=&slots[0]; slots[0].v.max_health=42;
  luaL_newmetatable(L,XGLUA_ENTITY_META);lua_pushvalue(L,-1);lua_setfield(L,-2,"__index");method(L,"IsValid",valid);method(L,"EntIndex",idx);method(L,"SetPos",Ent_SetPos);method(L,"SetCollisionBounds",Ent_SetCollisionBounds);method(L,"GetGroundEntity",Ent_GetGroundEntity);method(L,"GetMaxHealth",Ent_GetMaxHealth);method(L,"Spawn",Ent_Spawn);method(L,"Remove",Ent_Remove);lua_pop(L,1);
  bind(L,"Entity",Entity_Global);bind(L,"IsValid",valid);bind(L,"GetAll",ents_GetAll);bind(L,"FindByClass",ents_FindByClass);bind(L,"NullEntity",nullEntity);bind(L,"invalidate",invalidate);bind(L,"reuse",reuse);bind(L,"counts",counts);bind(L,"spatial",spatial);bind(L,"lookupCalls",lookupCalls);bind(L,"worldPrivate",worldPrivate);bind(L,"lifecycleCounts",lifecycleCounts);
  const char *src=R"(
    local w=Entity(0); assert(IsValid(w) and w:EntIndex()==0 and w==Entity(0))
    worldPrivate(false); assert(w:GetMaxHealth()==42); worldPrivate(true)
    assert(not pcall(function() w:Spawn() end) and not pcall(function() w:Remove() end))
    local so,ro=lifecycleCounts();assert(so==0 and ro==0)
    local p=Entity(1); p:Spawn();p:Remove();local sp,rp=lifecycleCounts();assert(sp==1 and rp==1)
    assert(p:GetGroundEntity()==w and ({[w]=42})[p:GetGroundEntity()]==42)
    local all=GetAll(); assert(#all==3 and all[1]==w and all[2]==p)
    local matches=FindByClass('worldspawn'); assert(#matches==1 and matches[1]==w)
    local wildcard=FindByClass('world*'); assert(#wildcard==1 and wildcard[1]==w)
    assert(NullEntity()==nil and Entity(3)==nil)
    local before=lookupCalls(); assert(Entity(-0.1)==nil and Entity(0.5)==nil and Entity(-1)==nil and Entity(4)==nil and Entity(1/0)==nil and Entity(0/0)==nil); assert(lookupCalls()==before)
    p:SetPos({1,2,3}); p:SetCollisionBounds({-1,-2,-3},{4,5,6})
    local x,y,z,mi,ma,sz,ow,sw=spatial(1);assert(x==1 and y==2 and z==3 and mi==-1 and ma==5 and sz==9 and ow and sw)
    local oc,sc=counts();assert(oc==1 and sc==1)
    assert(not pcall(function() p:SetPos({0/0,2,3}) end));assert(not pcall(function() p:SetCollisionBounds({4,0,0},{3,1,1}) end));assert(not pcall(function() p:SetCollisionBounds({0,0,0},{1,1,1/0}) end))
    local oc2,sc2=counts();assert(oc2==oc and sc2==sc)
    invalidate(1);assert(not IsValid(p));assert(not pcall(function() p:SetPos({1,2,3}) end));assert(not pcall(function() p:SetCollisionBounds({0,0,0},{1,1,1}) end));local o3,s3=counts();assert(o3==oc and s3==sc)
    reuse(1);local fresh=Entity(1);assert(fresh~=p and IsValid(fresh) and fresh:GetGroundEntity()==w)
  )";
  int result=luaL_dostring(L,src);if(result)std::printf("FAIL: %s\n",lua_tostring(L,-1));else std::puts("world, ground, lists, bounds and engine callbacks OK");lua_close(L);return result?1:0;
}
