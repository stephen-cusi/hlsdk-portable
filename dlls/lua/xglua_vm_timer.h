#ifndef XGLUA_VM_TIMER_H
#define XGLUA_VM_TIMER_H

#include "xglua_safe.h"

// timer.Tick dispatch. Lookup (timer global, Tick field) and execution share
// one protected call: both lookups honor __index metamethods, so a throwing
// metamethod cannot escape the frame callback. Missing or non-callable
// entries stay silently skipped; onError receives the error object on top
// for execution errors, as with lua_pcall. The caller's stack is preserved
// on every path.
static int XGLua_TimerTickCore( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_getglobal( L, "timer" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Tick" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	if( lua_pcall( L, 0, 0, 0 ) != 0 )
		return lua_error( L );	// report through the outer protected frame
	return 0;
}

static inline void XGLua_RunTimerTick( lua_State *L, void (*onError)( lua_State *, const char * ) )
{
	XGLua_ProtectedCall( L, XGLua_TimerTickCore, NULL, onError, "timer.Tick" );
}

#endif