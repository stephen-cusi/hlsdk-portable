#ifndef XGLUA_CL_PREDICT_H
#define XGLUA_CL_PREDICT_H

// Client-side SWEP P2 prediction bridge & restricted API guard.
// Runs inside HUD_WeaponsPostThink under XGLua_ProtectedCall.

// Thin header: zero hud/vgui dependencies so host tests can include it.
// Engine-side HUD includes live in xglua_cl_predict.cpp.
#include <cstring>
#include <cstdio>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include "lua/xglua_safe.h"

struct XGLuaCLPredictScope
{
	lua_State *L;
	bool prevFlag;
	bool active;

	XGLuaCLPredictScope( lua_State *state, bool enable );
	~XGLuaCLPredictScope();
};

bool XGLua_CL_IsPredicting( void );
void XGLua_CL_SetPredicting( bool predicting );

// Guard helper for Lua bindings that must NOT run during client prediction
// (e.g. damage, entity creation/removal, direct server commands).
inline void XGLua_CL_GuardNonPredictable( lua_State *L, const char *apiName )
{
	if( XGLua_CL_IsPredicting() )
	{
		luaL_error( L, "SWEP P2 predict: '%s' is not allowed inside prediction callback",
			apiName ? apiName : "restricted API" );
	}
}

// Registers prediction-safe SWEP helpers on client Lua state.
void XGLua_CL_RegisterPredictLibs( lua_State *L );

// Dispatches a SWEP method on the client VM if registered; safe under prediction.
void XGLua_CL_CallSWEPMethod( lua_State *L, const char *classname, const char *method );

#endif // XGLUA_CL_PREDICT_H
