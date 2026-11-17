#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "xglua_swep_order.h"

static bool snapshot(lua_State *L, const std::vector<int>& insertion, std::string &out)
{
    const char *names[] = {"weapon_zeta", "weapon_alpha", "weapon_mu", "weapon_base"};
    const char *primary[] = {"rockets", "9mm", "bolts", "none"};
    const char *secondary[] = {"energy", "grenades", "rockets", "none"};
    lua_pushliteral(L, "sentinel");
    lua_newtable(L);
    int registry = lua_gettop(L);
    for (size_t i=0; i<insertion.size(); ++i) {
        int j=insertion[i]; lua_newtable(L);
        lua_pushstring(L,primary[j]); lua_setfield(L,-2,"ammo1");
        lua_pushstring(L,secondary[j]); lua_setfield(L,-2,"ammo2");
        lua_pushboolean(L,j==3); lua_setfield(L,-2,"abstract");
        lua_setfield(L,registry,names[j]);
    }
    lua_pushinteger(L,42); lua_pushliteral(L,"ignored"); lua_rawset(L,registry);
    lua_pushliteral(L,"notweapon"); lua_pushinteger(L,1); lua_rawset(L,registry);
    int count=XGLua_PushSortedWeaponNames(L,-1);
    int sorted=lua_gettop(L);
    if(sorted!=registry+1 || count!=4) return false;
    const char *expectedNames[]={"weapon_alpha","weapon_base","weapon_mu","weapon_zeta"};
    for(int i=1;i<=count;++i) { lua_rawgeti(L,sorted,i); bool ok=std::string(lua_tostring(L,-1))==expectedNames[i-1]; lua_pop(L,1); if(!ok) return false; }
    bool occupied[32]={}; occupied[16]=occupied[18]=true;
    std::map<std::string,int> ammo; ammo["9mm"]=1; int nextAmmo=2;
    for (int i=1;i<=count;++i) {
        lua_rawgeti(L,sorted,i); std::string classname=lua_tostring(L,-1); lua_pop(L,1);
        lua_getfield(L,registry,classname.c_str());
        lua_getfield(L,-1,"abstract"); bool abstract=lua_toboolean(L,-1)!=0; lua_pop(L,1);
        if(abstract) { lua_pop(L,1); continue; }
        int id=16; while(id<31 && occupied[id]) ++id;
        if(id==31) return false;
        occupied[id]=true;
        char number[16]; std::sprintf(number,"%d",id); out+=classname+":"+number;
        const char *fields[]={"ammo1","ammo2"};
        for(int j=0;j<2;++j) {
            lua_getfield(L,-1,fields[j]); std::string name=lua_tostring(L,-1); lua_pop(L,1);
            if(!ammo.count(name)) ammo[name]=nextAmmo++;
            std::sprintf(number,"%d",ammo[name]); out+="/"+name+":"+number;
        }
        out+=";"; lua_pop(L,1);
    }
    lua_pop(L,2);
    bool result=lua_gettop(L)==1 && std::string(lua_tostring(L,1))=="sentinel";
    lua_pop(L,1); return result;
}
// A failing __index, like SWEP.Abstract lookup, must unwind to Lua pcall;
// the ordered name table remains on the Lua stack, not a live C++ vector.
static int errorDuringLookup(lua_State *L) {
    XGLua_PushSortedWeaponNames(L,1);
    lua_rawgeti(L,-1,1); lua_pushvalue(L,-1); lua_rawget(L,1);
    lua_getfield(L,-1,"Abstract");
    return 0;
}
int main() {
    std::vector<int> insertion={0,1,2,3}; std::string expected;
    for(int vm=0;vm<12;++vm) {
        lua_State *L=luaL_newstate(); if(!L) return 1; luaL_openlibs(L);
        do { std::string got; if(!snapshot(L,insertion,got)) { std::puts("FAIL: snapshot/stack"); return 1; }
             if(expected.empty()) expected=got;
             if(got!=expected) { std::puts("FAIL: unstable mapping"); return 1; }
        } while(std::next_permutation(insertion.begin(),insertion.end()));
        lua_close(L); insertion={0,1,2,3};
    }
    if(expected!="weapon_alpha:17/9mm:1/grenades:2;weapon_mu:19/bolts:3/rockets:4;weapon_zeta:20/rockets:4/energy:5;") { std::puts(expected.c_str()); return 1; }
    lua_State *L=luaL_newstate(); luaL_openlibs(L);
    if(luaL_dostring(L,"return {weapon_a=setmetatable({}, {__index=function() error('lookup failed') end})}")) return 1;
    lua_pushcfunction(L,errorDuringLookup); lua_pushvalue(L,1);
    int status=lua_pcall(L,1,0,0);
    if(status==0 || !lua_isstring(L,-1) || !std::strstr(lua_tostring(L,-1),"lookup failed") || lua_gettop(L)!=2) return 1;
    lua_close(L);
    std::puts("stable weapon and ammo order across VM instances and insertion orders OK"); return 0;
}
