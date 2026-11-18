#ifndef XGLUA_UTIL_TRACE_H
#define XGLUA_UTIL_TRACE_H

#include <cfloat>

// Server-only HL1 trace bindings. Options are intentionally a small, strict
// subset: this is not Source's mask/collisiongroup/custom-AABB trace API.
// Raw option reads never invoke a user table's __index metamethod.

static void XGLua_TraceOption( lua_State *L, const char *key )
{
	lua_pushstring( L, key );
	lua_rawget( L, 1 );
}

static void XGLua_CheckTraceKeys( lua_State *L, bool hull )
{
	lua_pushnil( L );
	while( lua_next( L, 1 ) )
	{
		if( lua_type( L, -2 ) != LUA_TSTRING )
			luaL_error( L, "trace options must use named fields" );
		const char *key = lua_tostring( L, -2 );
		if( strcmp( key, "start" ) && strcmp( key, "endpos" ) &&
			strcmp( key, "filter" ) && strcmp( key, "ignoremonsters" ) &&
			!( hull && !strcmp( key, "hull" ) ) )
			luaL_error( L, "unsupported trace option '%s'", key );
		lua_pop( L, 1 );
	}
}

static Vector XGLua_TraceVector( lua_State *L, const char *key )
{
	XGLua_TraceOption( L, key );
	Vector value = XGLua_CheckVector( L, -1 );
	if( !XGLua_IsFiniteVector( value ) )
		luaL_error( L, "trace %s must be a finite Vector", key );
	// Native sphere broadphase includes fourth-order float expressions.
	// Finite values near FLT_MAX are therefore not safe trace inputs. Keep
	// endpoint magnitudes below the fourth root with generous headroom for
	// differences and coefficients; this is not a BSP/map-size limit.
	const double limit = std::sqrt( std::sqrt( (double)FLT_MAX ) ) / 16.0;
	if( std::fabs( (double)value.x ) > limit || std::fabs( (double)value.y ) > limit ||
		std::fabs( (double)value.z ) > limit )
		luaL_error( L, "trace %s exceeds the coordinate safety limit", key );
	lua_pop( L, 1 );
	return value;
}

static void XGLua_TraceBool( lua_State *L, const char *key, bool value )
{
	lua_pushboolean( L, value );
	lua_setfield( L, -2, key );
}

static int XGLua_PushTraceResult( lua_State *L, const TraceResult &trace )
{
	// Some engines return world in pHit even on a miss. Entity identity is
	// meaningful only after determining whether the trace actually hit.
	const bool hit = trace.flFraction < 1.0f || trace.fStartSolid || trace.fAllSolid;
	edict_t *entity = hit && trace.pHit && !trace.pHit->free ? trace.pHit : NULL;
	const bool world = entity && entity == INDEXENT( 0 );
	lua_newtable( L );
	XGLua_TraceBool( L, "Hit", hit );
	XGLua_TraceBool( L, "StartSolid", trace.fStartSolid != 0 );
	XGLua_TraceBool( L, "AllSolid", trace.fAllSolid != 0 );
	XGLua_TraceBool( L, "HitWorld", world );
	XGLua_TraceBool( L, "HitNonWorld", entity && !world );
	XGLua_TraceBool( L, "InOpen", trace.fInOpen != 0 );
	XGLua_TraceBool( L, "InWater", trace.fInWater != 0 );
	lua_pushnumber( L, trace.flFraction );
	lua_setfield( L, -2, "Fraction" );
	lua_pushinteger( L, hit ? trace.iHitgroup : 0 );
	lua_setfield( L, -2, "HitGroup" );
	XGLua_PushVector( L, trace.vecEndPos );
	lua_setfield( L, -2, "HitPos" );
	// A miss or a trace starting in solid has no usable impact plane.
	XGLua_PushVector( L, hit && !trace.fAllSolid && !trace.fStartSolid ?
		Vector( trace.vecPlaneNormal ) : Vector( 0, 0, 0 ) );
	lua_setfield( L, -2, "HitNormal" );
	XGLua_PushEntity( L, entity );
	lua_setfield( L, -2, "Entity" );
	return 1;
}

static int XGLua_Trace( lua_State *L, bool hull )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	XGLua_CheckTraceKeys( L, hull );
	const Vector start = XGLua_TraceVector( L, "start" );
	const Vector end = XGLua_TraceVector( L, "endpos" );
	XGLua_TraceOption( L, "ignoremonsters" );
	if( !lua_isnil( L, -1 ) && lua_type( L, -1 ) != LUA_TBOOLEAN )
		return luaL_error( L, "trace ignoremonsters must be a boolean" );
	const int flags = lua_toboolean( L, -1 ) ? 1 : 0;
	lua_pop( L, 1 );
	int hullNumber = 0;
	if( hull )
	{
		XGLua_TraceOption( L, "hull" );
		if( lua_type( L, -1 ) != LUA_TNUMBER )
			return luaL_error( L, "TraceHull requires a numeric HULL_* constant" );
		const double value = lua_tonumber( L, -1 );
		if( !std::isfinite( value ) || value < 0 || value > 3 || std::floor( value ) != value )
			return luaL_error( L, "TraceHull hull must be an integer from 0 to 3" );
		hullNumber = (int)value;
		lua_pop( L, 1 );
	}
	XGLua_TraceOption( L, "filter" );
	edict_t *skip = lua_isnil( L, -1 ) ? NULL : XGLua_CheckEntity( L, -1 );
	lua_pop( L, 1 );
	TraceResult trace = {};
	trace.flFraction = 1.0f;
	trace.vecEndPos = end;
	if( hull )
	{
		if( !g_engfuncs.pfnTraceHull ) return luaL_error( L, "TraceHull engine API is unavailable" );
		TRACE_HULL( start, end, flags, hullNumber, skip, &trace );
	}
	else
	{
		if( !g_engfuncs.pfnTraceLine ) return luaL_error( L, "TraceLine engine API is unavailable" );
		TRACE_LINE( start, end, flags, skip, &trace );
	}
	return XGLua_PushTraceResult( L, trace );
}

static int util_TraceLine( lua_State *L ) { return XGLua_Trace( L, false ); }
static int util_TraceHull( lua_State *L ) { return XGLua_Trace( L, true ); }

#endif // XGLUA_UTIL_TRACE_H
