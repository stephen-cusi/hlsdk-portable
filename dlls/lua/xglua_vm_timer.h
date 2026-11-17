#ifndef XGLUA_VM_TIMER_H
#define XGLUA_VM_TIMER_H

// Preserve the caller's Lua stack on every timer dispatch path.
// onError receives the error object on top of the stack, as with lua_pcall.
static inline void XGLua_RunTimerTick( lua_State *L, void (*onError)( lua_State *, const char * ) )
{
	const int top = lua_gettop( L );
	lua_getglobal( L, "timer" );
	if( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Tick" );
		if( lua_isfunction( L, -1 ) && lua_pcall( L, 0, 0, 0 ) != 0 )
			onError( L, "timer.Tick" );
	}
	lua_settop( L, top );
}

#endif
