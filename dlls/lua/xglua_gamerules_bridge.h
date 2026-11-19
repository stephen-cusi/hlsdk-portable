#ifndef XGLUA_GAMERULES_BRIDGE_H
#define XGLUA_GAMERULES_BRIDGE_H

// Protected hook.Run dispatchers for CGameRules virtuals returning tri-state
// booleans, finite floats, integers, or entity handles.
//
// Boundary discipline: deals only in lua_State and POD structs; every Lua
// touch runs inside XGLua_ProtectedCall (xglua_safe.h) with only trivial
// locals across Lua API calls.

#include "xglua_safe.h"
#include "xglua_hook.h"
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdio>

// Tri-state boolean: 1 = strict true, 2 = strict false, 0 = nil/other/error.
// Reuses XGLua_HookRunBoolCore from xglua_hook.h.
static int XGLua_HookRunTriBool( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event )
		return 0;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookBoolCtx rc = { event, pushargs, argctx, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunBoolCore, &rc, onerror, szCtx ) )
		return 0;
	return rc.result;
}

// Finite float result: returns true and writes *out only when the hook chain
// returns a genuine Lua number within [-FLT_MAX, FLT_MAX].
struct XGLua_HookFloatCtx
{
	const char *event;
	XGLua_HookArgPush pushargs;
	void *argctx;
	float value;
	int got;
};

static int XGLua_HookRunFloatCore( lua_State *L, void *ctx )
{
	XGLua_HookFloatCtx *rc = (XGLua_HookFloatCtx *)ctx;
	rc->got = 0;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );
	if( lua_type( L, -1 ) == LUA_TNUMBER )
	{
		double v = lua_tonumber( L, -1 );
		if( std::isfinite( v ) && v >= -FLT_MAX && v <= FLT_MAX )
		{
			rc->value = (float)v;
			rc->got = 1;
		}
	}
	return 0;
}

static bool XGLua_HookRunFloat( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx, float *out,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event || !out )
		return false;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookFloatCtx rc = { event, pushargs, argctx, 0.0f, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunFloatCore, &rc, onerror, szCtx ) || !rc.got )
		return false;
	*out = rc.value;
	return true;
}

// Integer result: returns true and writes *out only when the hook chain
// returns a finite integral Lua number within [INT_MIN, INT_MAX].
struct XGLua_HookIntCtx
{
	const char *event;
	XGLua_HookArgPush pushargs;
	void *argctx;
	int value;
	int got;
};

static int XGLua_HookRunIntCore( lua_State *L, void *ctx )
{
	XGLua_HookIntCtx *rc = (XGLua_HookIntCtx *)ctx;
	rc->got = 0;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );
	if( lua_type( L, -1 ) == LUA_TNUMBER )
	{
		double v = lua_tonumber( L, -1 );
		if( std::isfinite( v ) && v >= INT_MIN && v <= INT_MAX && std::floor( v ) == v )
		{
			rc->value = (int)v;
			rc->got = 1;
		}
	}
	return 0;
}

static bool XGLua_HookRunInt( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx, int *out,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event || !out )
		return false;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookIntCtx rc = { event, pushargs, argctx, 0, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunIntCore, &rc, onerror, szCtx ) || !rc.got )
		return false;
	*out = rc.value;
	return true;
}

// Entity result: extracts a validated pointer via caller-supplied extractor
// (which must not raise on nil/non-entity, returning NULL instead).
typedef void *( *XGLua_EntExtractFn )( lua_State *L, int idx );

struct XGLua_HookEntCtx
{
	const char *event;
	XGLua_HookArgPush pushargs;
	void *argctx;
	XGLua_EntExtractFn extract;
	void *result;
};

static int XGLua_HookRunEntCore( lua_State *L, void *ctx )
{
	XGLua_HookEntCtx *rc = (XGLua_HookEntCtx *)ctx;
	rc->result = NULL;
	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "Run" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushstring( L, rc->event );
	const int nargs = 1 + ( rc->pushargs ? rc->pushargs( L, rc->argctx ) : 0 );
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );
	if( rc->extract )
		rc->result = rc->extract( L, -1 );
	return 0;
}

static void *XGLua_HookRunEntity( lua_State *L, const char *event,
	XGLua_HookArgPush pushargs, void *argctx, XGLua_EntExtractFn extract,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !event || !extract )
		return NULL;
	char szCtx[128];
	snprintf( szCtx, sizeof( szCtx ), "hook.Run(\"%s\")", event );
	XGLua_HookEntCtx rc = { event, pushargs, argctx, extract, NULL };
	if( !XGLua_ProtectedCall( L, XGLua_HookRunEntCore, &rc, onerror, szCtx ) )
		return NULL;
	return rc.result;
}

#endif // XGLUA_GAMERULES_BRIDGE_H
