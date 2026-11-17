#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <direct.h>
#include <process.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include "xglua_vm_include.h"
static std::map<std::string,std::string> files;
static int reads=0, coreErrors=0, closedStates=0;
static int markClosed(lua_State *) {++closedStates;return 0;}
static void watchClose(lua_State *L) {
    lua_newuserdata(L,1);lua_newtable(L);lua_pushcfunction(L,markClosed);
    lua_setfield(L,-2,"__gc");lua_setmetatable(L,-2);lua_setglobal(L,"_close_watch");
}
static void coreError(lua_State *L,const char *) { ++coreErrors; lua_pop(L,1); }
static XGLuaReadStatus reader(void *,const char *rel,char **out,size_t *len) {
    ++reads; if (!strcmp(rel,"pkg/io.lua")) return XGLUA_READ_ERROR;
    std::map<std::string,std::string>::const_iterator it=files.find(rel);
    if (it==files.end()) return XGLUA_READ_MISSING;
    *len=it->second.size(); *out=(char*)malloc(*len+1);
    if(!*out) return XGLUA_READ_ERROR;
    memcpy(*out,it->second.data(),*len);(*out)[*len]='\0'; return XGLUA_READ_OK;
}
static int include(lua_State *L) { int n=XGLua_IncludeImpl(L,reader,NULL); if(n<0)return lua_error(L); return n; }
static bool run(lua_State *L,const char *script) {
    int top=lua_gettop(L); int rc=luaL_loadbuffer(L,script,strlen(script),"=test");
    if(!rc)rc=lua_pcall(L,0,0,0);
    if(rc){std::printf("FAIL: %s\n",lua_tostring(L,-1));lua_settop(L,top);return false;}
    if(lua_gettop(L)!=top){std::puts("FAIL stack");lua_settop(L,top);return false;}
    return true;
}
int main() {
    files["pkg/main.lua"] = R"(
        marker=(marker or 0)+1
        assert(sharedglobal=="visible")
        local a,b,c=include("value.lua", "pkg"); assert(a=="local" and b==nil and c=="tail")
        assert(include("root.lua")=="root")
        assert(include("nested/entry.lua", "pkg")=="nested")
        delayed=function() return include("value.lua", "pkg") end
        return 123,nil,"last"
    )";
    files["pkg/value.lua"]="return 'local',nil,'tail'";
    files["value.lua"]="return 'wrong'";
    files["root.lua"]="return 'root'";
    files["pkg/nested/entry.lua"]="return include('../local.lua', 'pkg/nested')";
    files["pkg/local.lua"]="return 'nested'";
    files["pkg/bad.lua"]="invalid syntax ???";
    files["bad.lua"]="return 'should not execute'";
    files["pkg/fail.lua"]="error('local runtime error')";
    files["pkg/nonstring.lua"]="error({code=42})";
    files["fail.lua"]="return 'should not execute'";
    files["io.lua"]="return 'must not fall back on I/O failure'";
    files["empty.lua"]="";
    files["bom.lua"]=std::string("\xef\xbb\xbf")+"return 8";
    files["shortbom.lua"]=std::string("\xef\xbb");
    files["pkg/choice.lua"]="return 'outer'";
    files["pkg/nested/choice.lua"]="return 'inner'";
    files["choice.lua"]="return 'root choice'";
    files["pkg/outer.lua"]="local x=include('nested/tail.lua','pkg');return x";
    files["pkg/nested/tail.lua"]="return include('choice.lua','pkg/nested')";
    files["pkg/root_tail.lua"]="return include('choice.lua')";
    files["pkg/callback.lua"]="return function() return include('value.lua', 'pkg') end";
    files["pkg/global.lua"]="assert(getfenv(1)==_G);shared_written=71; rawset(getfenv(1),'shared_other',72); return SWEP and SWEP.value";
    files["pkg/gfallback.lua"]="return _G.include('value.lua', 'pkg')";
    lua_State *L=luaL_newstate();if(!L)return 1;luaL_openlibs(L);
    lua_pushcfunction(L,include);lua_setglobal(L,"include");
    lua_pushliteral(L,"sentinel");
    bool ok=run(L,R"(
        sharedglobal='visible'; SWEP={value=37}
        local a,b,c=include('pkg/main.lua'); assert(a==123 and b==nil and c=='last' and select('#',include('pkg/main.lua'))==3)
        local x,y,z=delayed();assert(x=='local' and y==nil and z=='tail')
        local cb=include('pkg/callback.lua');local x,y,z=cb();assert(x=='local' and y==nil and z=='tail')
        local x,y,z=include('pkg/gfallback.lua');assert(x=='local' and y==nil and z=='tail')
        assert(_G.include('pkg/global.lua')==37 and shared_written==71 and shared_other==72)
        assert(select('#',include('empty.lua'))==0 and include('bom.lua')==8)
        assert(not pcall(include,'shortbom.lua'))
        assert(not pcall(include,'../escape.lua'))
        assert(not pcall(include,'/absolute.lua') and not pcall(include,'\\\\server\\file.lua'))
        assert(not pcall(include,'C:\\file.lua') and not pcall(include,'embedded\0nul.lua'))
        assert(not pcall(include,'foo. /value.lua') and not pcall(include,'.. /outside.lua') and not pcall(include,'foo./value.lua'))
        assert(not pcall(include,string.rep('x',600)))
        assert(not pcall(include,'missing.lua'))
        assert(include('pkg\\value.lua')=='local')
        assert(include('pkg/outer.lua')=='inner' and include('pkg/root_tail.lua')=='root choice')
        assert(not pcall(include,'value.lua','../bad') and not pcall(include,'../../escape.lua','pkg'))
        assert(not pcall(include,'root.lua','pkg'))
    )");
    // Local error must not be hidden by fallback. Check a callback defined in a
    // file, not just direct invocation from a synthetic top-level chunk.
    files["pkg/errors.lua"]=R"(
        for _,name in ipairs({'bad.lua','fail.lua','io.lua'}) do
            local ok,err=pcall(include,name,'pkg'); assert(not ok and type(err)=='string')
        end
        local ok,err=pcall(include,'nonstring.lua','pkg');assert(not ok and type(err)=='table' and err.code==42)
        assert(not pcall(include,'../../escape.lua','pkg'))
    )";
    ok=run(L,"include('pkg/errors.lua')") && ok;
    // Exercise production disk reader against a unique local fixture.
    char root[128], luadir[160], name[192];
    snprintf(root,sizeof(root),"xgl_include_fixture_%d",_getpid());
    snprintf(luadir,sizeof(luadir),"%s/lua",root);
    if (_mkdir(root)!=0 || _mkdir(luadir)!=0) { std::puts("FAIL fixture dirs"); ok=false; }
    else {
        snprintf(name,sizeof(name),"%s/empty.lua",luadir);
        FILE *fp=fopen(name,"wb"); if(!fp || fclose(fp)!=0) ok=false;
        char *data=NULL;size_t n=99;
        if(XGLua_ReadDiskScript(root,"empty.lua",&data,&n)!=XGLUA_READ_OK || !data || n!=0)ok=false;
        free(data);data=NULL;
        if(XGLua_ReadDiskScript(root,"missing.lua",&data,&n)!=XGLUA_READ_MISSING)ok=false;
        if(remove(name)!=0 || _rmdir(luadir)!=0 || _rmdir(root)!=0)ok=false;
    }
    if(lua_gettop(L)!=1 || strcmp(lua_tostring(L,1),"sentinel"))ok=false;
    lua_close(L);
    // Exercise production bootstrap success, syntax failure, runtime failure
    // and missing file. A failed state must be closed and pointer cleared.
    files["includes/init.lua"]="booted=true";
    lua_State *vm=luaL_newstate();luaL_openlibs(vm);watchClose(vm);lua_pushliteral(vm,"sentinel");
    if(!XGLua_RunCoreInit(&vm,vm,reader,NULL,coreError) || !vm)ok=false;
    if(vm) { if(lua_gettop(vm)!=1)ok=false; lua_getglobal(vm,"booted");if(!lua_toboolean(vm,-1))ok=false;lua_pop(vm,1);lua_close(vm); }
    const char *badinit[]={"?invalid", "error({})"};
    for(size_t i=0;i<2;++i) {
        files["includes/init.lua"]=badinit[i];vm=luaL_newstate();luaL_openlibs(vm);watchClose(vm);
        if(XGLua_RunCoreInit(&vm,vm,reader,NULL,coreError) || vm)ok=false;
    }
    files["includes/init.lua"]="include('includes/not-present.lua')";
    vm=luaL_newstate();luaL_openlibs(vm);watchClose(vm);lua_pushcfunction(vm,include);lua_setglobal(vm,"include");
    if(XGLua_RunCoreInit(&vm,vm,reader,NULL,coreError) || vm)ok=false;
    files.erase("includes/init.lua");vm=luaL_newstate();luaL_openlibs(vm);watchClose(vm);
    if(XGLua_RunCoreInit(&vm,vm,reader,NULL,coreError) || vm || coreErrors!=4 || closedStates!=5)ok=false;
    if(ok)std::puts("include root/explicit paths, tail calls, errors, returns and globals OK");
    return ok?0:1;
}
