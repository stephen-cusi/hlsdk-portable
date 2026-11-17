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

// hook.Run dispatch whose first result is reduced to a strict boolean.
// Only a genuine Lua true (LUA_TBOOLEAN) counts: numbers, strings and nil do
// not, matching the explicit-opt-in contract of decision hooks like
// PlayerLoadout where anything else must fall back to vanilla behavior.
// Hook priority and the GAMEMODE fallback stay inside hook.Run: a plain hook
// returning non-nil terminates the chain before the GM function runs. A
// raising GM function (hook.lua calls it unprotected) is reported through
// onerror and yields false; the stack is restored either way.
struct XGLua_HookBoolCtx { const char *event; XGLua_HookArgPush pushargs; void *argctx; int result; };

static int XGLua_HookRunBoolCore( lua_State *L, void *ctx )
{
	XGLua_HookBoolCtx *rc = (XGLua_HookBoolCtx *)ctx;
	rc->result = 0;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );		// [Run, event]
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );		// report through the outer protected frame
	if( lua_type( L, -1 ) == LUA_TBOOLEAN && lua_toboolean( L, -1 ) )
		rc->result = 1;
	return 0;	// outer frame discards the result value
}

static bool XGLua_HookRunBool( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event )
		return false;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookBoolCtx rc = { event, pushargs, argctx, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunBoolCore, &rc, onerror, szCtx ) )
		return false;
	return rc.result != 0;
}

// hook.Run dispatch capturing a string result for text-rewrite hooks like
// PlayerSay. Returns 1 when the first result is a string (copied into out,
// truncated to fit; the stack is restored either way) and 0 when nothing
// usable came back: no result at all, or a non-string value - the documented
// safe fallback where the native text passes through untouched. Lua errors
// are reported through onerror and also return 0. Hook priority and the
// GAMEMODE fallback stay inside hook.Run: a plain hook returning non-nil
// terminates the chain before the GM function runs, so a hook returning a
// non-string also suppresses the GM fallback (caller-visible behavior: 0).
struct XGLua_HookStringCtx { const char *event; XGLua_HookArgPush pushargs; void *argctx; char *out; size_t outlen; int got; };

static int XGLua_HookRunStringCore( lua_State *L, void *ctx )
{
	XGLua_HookStringCtx *rc = (XGLua_HookStringCtx *)ctx;
	rc->got = 0;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );		// [Run, event]
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );		// report through the outer protected frame
	if( lua_type( L, -1 ) == LUA_TSTRING )
	{
		snprintf( rc->out, rc->outlen, "%s", lua_tostring( L, -1 ) );
		rc->got = 1;
	}
	return 0;	// outer frame discards the result value
}

static int XGLua_HookRunString( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx, char *out, size_t outlen,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event || !out || !outlen )
		return 0;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookStringCtx rc = { event, pushargs, argctx, out, outlen, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunStringCore, &rc, onerror, szCtx ) )
		return 0;
	return rc.got;
}

#endif // XGLUA_HOOK_H
