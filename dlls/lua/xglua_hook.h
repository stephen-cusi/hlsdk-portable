#ifndef XGLUA_HOOK_H
#define XGLUA_HOOK_H

#include "xglua_safe.h"

// hook.Run dispatch shared by every engine callback (Think, Initialize,
// InitPostEntity, ShutDown, PlayerSpawn, ...). Lookup and execution share one
// protected call: lua_getglobal/lua_getfield honor __index metamethods on _G
// and on the hook table, so a throwing metamethod is reported exactly like a
// throwing callback instead of escaping into the engine.
//
// Preserved semantics: a missing hook global, a non-table hook, a missing or
// non-function Run stays silently skipped (no error log); callback errors are
// reported through onerror with the same "hook.Run(\"<event>\")" context the
// VM layer used before; results are still discarded.
//
// Stack protocol (Lua 5.1: callee first, arguments on top): the hook table
// and the Run function are pushed before the arguments, so the call is
// [Run, event, args...] with the func slot at top-nargs-1. Anything left on
// the stack when the guarded function returns 0 is discarded by the outer
// frame, so no index juggling is needed. Extra arguments are pushed by
// pushargs inside the protected region, so argument allocation (entity
// userdata) cannot raise unprotected either.

typedef int ( *XGLua_HookArgPush )( lua_State *L, void *ctx );

struct XGLua_HookRunCtx { const char *event; XGLua_HookArgPush pushargs; void *argctx; };

static int XGLua_HookRunCore( lua_State *L, void *ctx )
{
	XGLua_HookRunCtx *rc = (XGLua_HookRunCtx *)ctx;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );		// [Run, event]
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 0, 0 ) != 0 )
		return lua_error( L );		// report through the outer protected frame
	return 0;
}

static void XGLua_HookRunProtected( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event )
		return;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookRunCtx rc = { event, pushargs, argctx };
	XGLua_ProtectedCall( L, XGLua_HookRunCore, &rc, onerror, szCtx );
}

#endif // XGLUA_HOOK_H
