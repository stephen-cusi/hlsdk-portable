#ifndef XGLUA_ADDON_LOAD_H
#define XGLUA_ADDON_LOAD_H

#include "xglua_addon_weapons.h"
#include "xglua_vm_include.h"
#include "xglua_safe.h"

struct XGLua_AddonLoadCtx
{
    const char *path;
    XGLuaReadScript read;
    void *readCtx;
};

static int XGLua_AddonLoadCore( lua_State *L, void *ud )
{
    XGLua_AddonLoadCtx *ctx = (XGLua_AddonLoadCtx *)ud;
    lua_newtable( L );
    lua_setglobal( L, "SWEP" );
    if( XGLua_ExecuteScript( L, ctx->path, ctx->read, ctx->readCtx, 0 ) != XGLUA_READ_OK )
        return lua_error( L );
    return 0;
}

static void XGLua_LoadAddonWeapons( lua_State *L, const char *root,
    XGLuaReadScript read, void *readCtx, void (*report)(lua_State *, const char *) )
{
    std::vector<std::string> files;
    XGLua_FindAddonWeapons( root, files );
    for( size_t i = 0; i < files.size(); ++i )
    {
        XGLua_AddonLoadCtx ctx = { files[i].c_str(), read, readCtx };
        XGLua_ProtectedCall( L, XGLua_AddonLoadCore, &ctx, report, ctx.path );
    }
}
#endif
