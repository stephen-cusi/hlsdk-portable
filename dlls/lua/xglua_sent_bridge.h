#ifndef XGLUA_SENT_BRIDGE_H
#define XGLUA_SENT_BRIDGE_H

// Protected bridge between CLuaEntity (C++) and scripted_ents (Lua).
//
// Boundary discipline: deals only in lua_State and POD structs (no game
// classes), so host tests can include this header directly against the real
// scripted_ents.lua. Every engine-to-Lua entry runs inside XGLua_ProtectedCall
// with only trivial locals across Lua API calls (xglua_safe.h).

#include <cstring>
#include <cstdio>
#include "xglua_safe.h"
#include "xglua_hook.h"

#define XGLUA_SENT_REGISTRY "XGLua_SENTTables"

// Raw registry access: returns registry[XGLUA_SENT_REGISTRY] = { [classname] = ENT table }.
static void XGLua_GetSENTRegistryImpl( lua_State *L )
{
	lua_pushlightuserdata( L, (void *)XGLUA_SENT_REGISTRY );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if( lua_istable( L, -1 ) )
		return;
	lua_pop( L, 1 );
	lua_newtable( L );
	lua_pushvalue( L, -1 );
	lua_pushlightuserdata( L, (void *)XGLUA_SENT_REGISTRY );
	lua_insert( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

// Store a validated ENT table into the C++-side registry (rawset).
static bool XGLua_StoreSENTTable( lua_State *L, int tableIdx, const char *classname )
{
	if( !L || !classname || !classname[0] )
		return false;
	if( tableIdx < 0 )
		tableIdx += lua_gettop( L ) + 1;
	if( !lua_istable( L, tableIdx ) )
		return false;

	XGLua_GetSENTRegistryImpl( L );
	lua_pushstring( L, classname );
	lua_pushvalue( L, tableIdx );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	return true;
}

// Abstract must be read raw so concrete subclasses do not inherit Abstract=true
// from base_entity / base_anim / base_point.
static bool XGLua_SENTIsAbstract( lua_State *L, int tableIdx )
{
	if( tableIdx < 0 )
		tableIdx += lua_gettop( L ) + 1;
	lua_pushliteral( L, "Abstract" );
	lua_rawget( L, tableIdx );
	const bool isAbstract = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );
	return isAbstract;
}

// Validate ENT.Type (honors Base inheritance via lua_getfield; defaults to "anim").
static bool XGLua_ReadSENTType( lua_State *L, int tableIdx, char *out, size_t outlen )
{
	if( !out || outlen < 8 )
		return false;
	if( tableIdx < 0 )
		tableIdx += lua_gettop( L ) + 1;

	lua_getfield( L, tableIdx, "Type" );
	if( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		snprintf( out, outlen, "%s", "anim" );
		return true;
	}
	if( lua_type( L, -1 ) != LUA_TSTRING )
	{
		lua_pop( L, 1 );
		return false;
	}
	const char *s = lua_tostring( L, -1 );
	const bool valid = s && ( !strcmp( s, "anim" ) || !strcmp( s, "point" ) || !strcmp( s, "brush" ) );
	if( valid )
		snprintf( out, outlen, "%s", s );
	lua_pop( L, 1 );
	return valid;
}

// Protected registry existence check (classname push can allocate).
struct XGLua_IsLuaEntityCtx { const char *classname; bool found; };

static int XGLua_IsLuaEntityCore( lua_State *L, void *ud )
{
	XGLua_IsLuaEntityCtx *ctx = (XGLua_IsLuaEntityCtx *)ud;
	XGLua_GetSENTRegistryImpl( L );
	lua_pushstring( L, ctx->classname );
	lua_rawget( L, -2 );
	ctx->found = lua_istable( L, -1 ) != 0;
	return 0;
}

static bool XGLua_IsLuaEntityImpl( lua_State *L, const char *classname,
	void ( *onerror )( lua_State *, const char * ) )
{
	if( !L || !classname || !classname[0] )
		return false;
	XGLua_IsLuaEntityCtx ctx = { classname, false };
	XGLua_ProtectedCall( L, XGLua_IsLuaEntityCore, &ctx, onerror, "entity registry lookup" );
	return ctx.found;
}

// Protected ENT:Method(...) dispatch (void return).
// Stack order (Lua 5.1 callee-first): [methodFn, self, extraArgs...]
struct XGLua_SENTMethodCtx
{
	const char *classname;
	const char *method;
	XGLua_HookArgPush pushargs;
	void *argctx;
	int result; // tri-state for bool variant: 0=nil/other, 1=true, 2=false
};

static int XGLua_CallSENTMethodCore( lua_State *L, void *ud )
{
	XGLua_SENTMethodCtx *mc = (XGLua_SENTMethodCtx *)ud;
	XGLua_GetSENTRegistryImpl( L );
	lua_pushstring( L, mc->classname );
	lua_rawget( L, -2 );
	if( !lua_istable( L, -1 ) )
		return 0;
	// lua_getfield honors Base __index inheritance inside this protected frame.
	lua_getfield( L, -1, mc->method );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	const int nargs = mc->pushargs ? mc->pushargs( L, mc->argctx ) : 0;
	if( lua_pcall( L, nargs, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

static void XGLua_CallSENTMethod( lua_State *L, const char *classname, const char *method,
	XGLua_HookArgPush pushargs, void *argctx,
	void ( *onerror )( lua_State *, const char * ), const char *context )
{
	if( !L || !classname || !classname[0] || !method )
		return;
	XGLua_SENTMethodCtx mc = { classname, method, pushargs, argctx, 0 };
	XGLua_ProtectedCall( L, XGLua_CallSENTMethodCore, &mc, onerror, context );
}

// Protected ENT:Method(...) dispatch returning tri-state boolean:
//   1 = strict Lua true, 2 = strict Lua false, 0 = nil / non-boolean / missing / error
static int XGLua_CallSENTBoolCore( lua_State *L, void *ud )
{
	XGLua_SENTMethodCtx *mc = (XGLua_SENTMethodCtx *)ud;
	mc->result = 0;
	XGLua_GetSENTRegistryImpl( L );
	lua_pushstring( L, mc->classname );
	lua_rawget( L, -2 );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, mc->method );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	const int nargs = mc->pushargs ? mc->pushargs( L, mc->argctx ) : 0;
	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );
	if( lua_type( L, -1 ) == LUA_TBOOLEAN )
		mc->result = lua_toboolean( L, -1 ) ? 1 : 2;
	return 0;
}

static int XGLua_CallSENTBoolMethod( lua_State *L, const char *classname, const char *method,
	XGLua_HookArgPush pushargs, void *argctx,
	void ( *onerror )( lua_State *, const char * ), const char *context )
{
	if( !L || !classname || !classname[0] || !method )
		return 0;
	XGLua_SENTMethodCtx mc = { classname, method, pushargs, argctx, 0 };
	if( !XGLua_ProtectedCall( L, XGLua_CallSENTBoolCore, &mc, onerror, context ) )
		return 0;
	return mc.result;
}

#endif // XGLUA_SENT_BRIDGE_H
