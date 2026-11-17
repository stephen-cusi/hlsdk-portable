#ifndef XGLUA_VM_INCLUDE_H
#define XGLUA_VM_INCLUDE_H

// Include source paths are lexical paths under lua/. This is not a sandbox:
// symbolic links and other Lua APIs are not confined by this module.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>

#define XGLUA_INCLUDE_PATH_MAX 512

enum XGLuaReadStatus { XGLUA_READ_OK, XGLUA_READ_MISSING, XGLUA_READ_ERROR };
typedef XGLuaReadStatus (*XGLuaReadScript)(void *, const char *, char **, size_t *);

// Collapse '.', '..' and separators while refusing paths that escape lua/.
// The explicit base directory and requested path are normalized as one path.
static bool XGLua_IncludePath(const char *base, const char *path, size_t pathlen, char *out, size_t outlen)
{
    if (!pathlen || !outlen || path[0]=='/' || path[0]=='\\' || memchr(path, '\0', pathlen)) return false;
    char joined[XGLUA_INCLUDE_PATH_MAX * 2];
    size_t n = 0;
    if (base && *base) {
        size_t b = strlen(base);
        if (b + 1 >= sizeof(joined)) return false;
        memcpy(joined, base, b); n = b; joined[n++] = '/';
    }
    if (pathlen >= sizeof(joined) - n) return false;
    memcpy(joined + n, path, pathlen); n += pathlen;
    size_t used=0, i=0;
    while (i<n) {
        while (i<n && (joined[i]=='/' || joined[i]=='\\')) ++i;
        size_t start=i;
        while (i<n && joined[i]!='/' && joined[i]!='\\') {
            unsigned char c=(unsigned char)joined[i];
            if (c<32 || c==':' || c==127) return false;
            ++i;
        }
        size_t len=i-start;
        if (!len || (len==1 && joined[start]=='.')) continue;
        if (len==2 && joined[start]=='.' && joined[start+1]=='.') {
            if (!used) return false;
            while (used && out[used-1]!='/') --used;
            if (used) --used;
            continue;
        }
        // Win32 normalizes trailing spaces/dots in path components. Reject
        // ambiguous aliases on every platform for consistent resolution.
        if (joined[start+len-1]=='.' || joined[start+len-1]==' ') return false;
        if (used) { if (used+1 >= outlen) return false; out[used++]='/'; }
        if (len >= outlen-used) return false;
        memcpy(out+used, joined+start, len); used+=len;
    }
    if (!used) return false;
    out[used]='\0';
    return true;
}

// Reader result has malloc-owned data on success (also for empty scripts).
// The pcall contains Lua errors; on success the chunk's results stay on stack,
// on failure exactly one error object stays on stack.
static int XGLua_ExecuteScript(lua_State *L, const char *rel, XGLuaReadScript read, void *ctx, int nresults)
{
    char *data=NULL; size_t len=0;
    XGLuaReadStatus status=read(ctx, rel, &data, &len);
    if (status != XGLUA_READ_OK) {
        lua_pushfstring(L, status==XGLUA_READ_MISSING ? "include: script not found: %s" : "include: cannot read script: %s", rel);
        return (int)status;
    }
    const char *chunk=data;
    if (len>=3 && (unsigned char)chunk[0]==0xef && (unsigned char)chunk[1]==0xbb && (unsigned char)chunk[2]==0xbf) { chunk+=3; len-=3; }
    char name[XGLUA_INCLUDE_PATH_MAX+6];
    int written=snprintf(name,sizeof(name),"@lua/%s",rel);
    if (written<0 || (size_t)written>=sizeof(name)) { free(data); lua_pushliteral(L,"include: source path too long"); return (int)XGLUA_READ_ERROR; }
    int code=luaL_loadbuffer(L,chunk,len,name);
    free(data);
    if (code) return (int)XGLUA_READ_ERROR;
    if (lua_pcall(L,0,nresults,0)) return (int)XGLUA_READ_ERROR;
    return (int)XGLUA_READ_OK;
}

// This function returns a count or -1 with one error on top. The outer C
// wrapper raises only after returning, with no C++ destructors in flight.
static int XGLua_IncludeImpl(lua_State *L, XGLuaReadScript read, void *ctx)
{
    size_t len=0;
    const char *path=luaL_checklstring(L,1,&len);
    // One-argument include remains relative to lua/. A second, explicit
    // directory argument opts into deterministic relative resolution.
    // LuaJIT may erase a tail caller's frame; guessing its source silently
    // loads the wrong file when a caller has an identically named sibling.
    char base[XGLUA_INCLUDE_PATH_MAX]="";
    if (!lua_isnoneornil(L,2)) {
        size_t baselen=0;
        const char *rawbase=luaL_checklstring(L,2,&baselen);
        if (baselen && !XGLua_IncludePath(NULL,rawbase,baselen,base,sizeof(base))) {
            lua_pushliteral(L,"include: invalid base directory"); return -1;
        }
    }
    char relative[XGLUA_INCLUDE_PATH_MAX];
    if (!XGLua_IncludePath(base,path,len,relative,sizeof(relative))) { lua_pushliteral(L,"include: invalid or escaping path"); return -1; }
    int top=lua_gettop(L);
    int result=XGLua_ExecuteScript(L,relative,read,ctx,LUA_MULTRET);
    if (result!=(int)XGLUA_READ_OK) return -1;
    return lua_gettop(L)-top;
}

// Run the mandatory bootstrap and leave no partial VM behind on failure.
// The callback reports/pops the error before closing, as production LuaError.
static bool XGLua_RunCoreInit(lua_State **slot, lua_State *L, XGLuaReadScript read,
                              void *ctx, void (*onError)(lua_State *, const char *))
{
    int top=lua_gettop(L);
    int status=XGLua_ExecuteScript(L,"includes/init.lua",read,ctx,0);
    if (status == XGLUA_READ_OK) { lua_settop(L,top); return true; }
    onError(L,"core init");
    lua_settop(L,top);
    *slot=NULL;
    lua_close(L);
    return false;
}

static XGLuaReadStatus XGLua_ReadDiskScript(void *ctx,const char *rel,char **out,size_t *len)
{
    char path[1024];
    int n=snprintf(path,sizeof(path),"%s/lua/%s",(const char *)ctx,rel);
    if (n<0 || (size_t)n>=sizeof(path)) return XGLUA_READ_ERROR;
    errno=0;
    FILE *fp=fopen(path,"rb");
    if (!fp) return errno==ENOENT ? XGLUA_READ_MISSING : XGLUA_READ_ERROR;
    if (fseek(fp,0,SEEK_END)!=0) { fclose(fp); return XGLUA_READ_ERROR; }
    long size=ftell(fp);
    if (size<0 || fseek(fp,0,SEEK_SET)!=0) { fclose(fp); return XGLUA_READ_ERROR; }
    char *data=(char *)malloc((size_t)size+1);
    if (!data) { fclose(fp); return XGLUA_READ_ERROR; }
    size_t got=fread(data,1,(size_t)size,fp);
    bool bad=(got!=(size_t)size || ferror(fp));
    if (fclose(fp)!=0) bad=true;
    if (bad) { free(data); return XGLUA_READ_ERROR; }
    data[got]='\0'; *out=data; *len=got;
    return XGLUA_READ_OK;
}
#endif
