#ifndef XGLUA_SAFE_H
#define XGLUA_SAFE_H

// LuaJIT headers have no extern "C" guard of their own — wrap them here.
extern "C" {
#include "lauxlib.h"
}

// Protected boundary for every engine-to-Lua entry.
//
// LuaJIT reports errors with longjmp: outside an active protected frame an
// error reaches the panic handler and exits the process, and even a caught
// longjmp skips C++ destructors in the frames it unwinds. Both are fatal for
// engine callbacks, so nothing that can raise (metamethod-honoring lookups,
// allocations, user code) may run unprotected.
//
// The boundary is lua_cpcall: its errframe is established in lj_vm_cpcall
// before the helper allocates the trampoline C function (lj_api.c), so the
// boundary itself cannot raise, and the per-call trampoline allocation is
// covered as well. Dispatchers must keep only Lua stack values and trivial
// locals alive across Lua API calls — the same discipline xglua_swep_order.h
// documents; data flows through ctx raw pointers or static storage, so a
// longjmp has nothing to unwind.
//
// Contract for the guarded function:
//   - it receives ctx; lua_cpcall discards return values, so guarded
//     functions are dispatch-only and must return 0;
//   - lua_cpcall gives the entry a fresh C-function frame containing ctx as
//     its only argument. The entry pops it; values in the caller's frame are
//     not directly addressable, so pass required Lua objects through stable
//     registry references in ctx;
//   - nested calls follow the Lua 5.1 protocol: the callee is pushed first
//     and the arguments on top of it (func at top-nargs-1). A guarded
//     function that performs its own lua_pcall propagates failures with
//     `return lua_error(L)` so the outer frame reports them.
// On any error onerror receives the error object on top (and pops it, like
// the production error reporter); the stack is restored to its state at
// entry on both success and failure.

typedef int (*XGLuaGuardedFn)( lua_State *L, void *ctx );

struct XGLua_GuardedBind { XGLuaGuardedFn fn; void *ctx; };

static int XGLua_GuardedEntry( lua_State *L )
{
	XGLua_GuardedBind *bind = (XGLua_GuardedBind *)lua_touserdata( L, 1 );
	lua_pop( L, 1 );
	return bind->fn( L, bind->ctx );
}

// lua_cpcall always requests zero results, so this helper is dispatch-only.
static bool XGLua_ProtectedCall( lua_State *L, XGLuaGuardedFn fn, void *ctx,
	void ( *onerror )( lua_State *, const char * ), const char *context )
{
	if( !L || !fn )
		return false;
	XGLua_GuardedBind bind = { fn, ctx };
	const int top = lua_gettop( L );
	if( lua_cpcall( L, XGLua_GuardedEntry, &bind ) != 0 )
	{
		if( onerror )
			onerror( L, context );
		else
			lua_pop( L, 1 );
		lua_settop( L, top );
		return false;
	}
	lua_settop( L, top );
	return true;
}

// Type-safe error text for reporters that run outside a protected frame:
// lua_tostring on non-strings either returns NULL or allocates a number
// conversion; both are wrong there. Strings are taken verbatim, everything
// else degrades to its type name, which never allocates.
static const char *XGLua_ErrorText( lua_State *L, int idx )
{
	int type = lua_type( L, idx );
	if( type == LUA_TSTRING )
		return lua_tostring( L, idx );
	return lua_typename( L, type );
}

#endif // XGLUA_SAFE_H
