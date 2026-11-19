#ifndef XGLUA_DAMAGE_H
#define XGLUA_DAMAGE_H

// PlayerUse / PlayerShouldTakeDamage / EntityTakeDamage dispatch and the
// DamageInfo userdata (design doc 8.1 event table, M2).
//
// Boundary discipline: this header only deals in lua_State and edict_t - no
// game classes - so tests can include it with stubbed XGLua_PushEntity /
// XGLua_ReportError / g_xglua_L. Exactly ONE non-test TU includes it
// (xglua_damage.cpp): the live-DamageInfo guard below must be a single
// object per binary.
//
// DamageInfo lifetime: the userdata is a VIEW over a stack-local context
// that exists only for the duration of one EntityTakeDamage dispatch.
// Accessors compare against the live-context pointer, so a DamageInfo kept
// by a script (or reached after a nested dispatch replaced the live slot)
// fails with an explicit Lua error instead of ever dereferencing stale C++
// state. No pointer it holds is touched outside the dispatch.

#include "xglua_safe.h"
#include "xglua_hook.h"
#include <cmath>
#include <cfloat>
#include <climits>

// Provided by the game (xglua.h) or by tests with identical signatures.
void XGLua_ReportError( lua_State *L, const char *context );
void XGLua_PushEntity( lua_State *L, edict_t *pent );
bool XGLua_DamageEntityValid( edict_t *pent, int serial );
extern lua_State *g_xglua_L;

//===========================================================================
// DamageInfo userdata
//===========================================================================

struct XGLuaDmgCtx
{
	edict_t *victim;
	edict_t *inflictor;	// NULL when the caller had no valid inflictor
	edict_t *attacker;	// NULL when the caller had no valid attacker
	float *damage;		// live view into the caller's damage value
	int *type;		// live view into the caller's damage-type bits
	unsigned long long generation;
	int inflictorSerial;
	int attackerSerial;
};

// Single live dispatch slot; nested dispatches save/restore it.
static XGLuaDmgCtx *g_pXGLuaDmgLive = NULL;
static unsigned long long g_XGLuaDmgGeneration = 0;

struct XGLuaDmgUD { XGLuaDmgCtx *ctx; unsigned long long generation; };
#define XGLUA_DMGINFO_META "XGLua_DamageInfo"

static XGLuaDmgCtx *XGLua_CheckDmgInfo( lua_State *L, int idx )
{
	XGLuaDmgUD *ud = (XGLuaDmgUD *)luaL_checkudata( L, idx, XGLUA_DMGINFO_META );
	if( !g_pXGLuaDmgLive || ud->ctx != g_pXGLuaDmgLive ||
		ud->generation != g_pXGLuaDmgLive->generation )
		luaL_error( L, "Tried to use a DamageInfo outside its EntityTakeDamage call!" );
	return ud->ctx;
}

static int XGLuaDmg_GetDamage( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	lua_pushnumber( L, (double)*ctx->damage );
	return 1;
}

static int XGLuaDmg_SetDamage( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	double value = luaL_checknumber( L, 2 );
	if( !std::isfinite( value ) || value < -FLT_MAX || value > FLT_MAX )
		return luaL_error( L, "DamageInfo: damage must be a finite float" );
	*ctx->damage = (float)value;
	return 0;
}

static int XGLuaDmg_GetDamageType( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	lua_pushnumber( L, (double)*ctx->type );
	return 1;
}

static int XGLuaDmg_SetDamageType( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	double value = luaL_checknumber( L, 2 );
	if( !std::isfinite( value ) || value < INT_MIN || value > INT_MAX || std::floor( value ) != value )
		return luaL_error( L, "DamageInfo: damage type must be an integer in range" );
	*ctx->type = (int)value;
	return 0;
}

static int XGLuaDmg_GetAttacker( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	if( ctx->attacker && XGLua_DamageEntityValid( ctx->attacker, ctx->attackerSerial ) )
		XGLua_PushEntity( L, ctx->attacker );
	else
		lua_pushnil( L );
	return 1;
}

static int XGLuaDmg_GetInflictor( lua_State *L )
{
	XGLuaDmgCtx *ctx = XGLua_CheckDmgInfo( L, 1 );
	if( ctx->inflictor && XGLua_DamageEntityValid( ctx->inflictor, ctx->inflictorSerial ) )
		XGLua_PushEntity( L, ctx->inflictor );
	else
		lua_pushnil( L );
	return 1;
}

static void XGLua_PushDmgInfoMetatable( lua_State *L )
{
	luaL_newmetatable( L, XGLUA_DMGINFO_META );
	// Install methods once; later dispatches reuse the registry entry.
	lua_getfield( L, -1, "GetDamage" );
	const bool installed = !lua_isnil( L, -1 );
	lua_pop( L, 1 );
	if( !installed )
	{
		static const luaL_Reg methods[] =
		{
			{ "GetDamage", XGLuaDmg_GetDamage },
			{ "SetDamage", XGLuaDmg_SetDamage },
			{ "GetDamageType", XGLuaDmg_GetDamageType },
			{ "SetDamageType", XGLuaDmg_SetDamageType },
			{ "GetAttacker", XGLuaDmg_GetAttacker },
			{ "GetInflictor", XGLuaDmg_GetInflictor },
			{ NULL, NULL }
		};
		for( const luaL_Reg *m = methods; m->name; ++m )
		{
			lua_pushcfunction( L, m->func );
			lua_setfield( L, -2, m->name );
		}
		lua_pushvalue( L, -1 );
		lua_setfield( L, -2, "__index" );
	}
	// Leave the metatable on the stack; XGLua_PushDmgArgs immediately applies
	// it to the userdata with lua_setmetatable.
}

//===========================================================================
// dispatch implementations (edict-only boundary)
//===========================================================================

static int XGLua_PushDmgArgs( lua_State *L, void *ud )
{
	XGLuaDmgCtx *ctx = (XGLuaDmgCtx *)ud;
	XGLua_PushEntity( L, ctx->victim );
	XGLuaDmgUD *info = (XGLuaDmgUD *)lua_newuserdata( L, sizeof( XGLuaDmgUD ) );
	info->ctx = ctx;
	info->generation = ctx->generation;
	XGLua_PushDmgInfoMetatable( L );
	lua_setmetatable( L, -2 );
	return 2;
}

// EntityTakeDamage(ent, dmginfo): strict boolean true from the hook chain
// cancels the damage (caller returns without native handling); any other
// outcome - nil, false, non-boolean, no handlers, or a reported Lua error -
// continues into the native path with whatever damage/type the script wrote
// through the DamageInfo view (live-coupled: SetDamage writes the caller's
// variables directly).
static int XGLua_EntityTakeDamageImpl( lua_State *L, XGLuaDmgCtx *ctx )
{
	if( !L || !ctx )
		return 0;
	float damage = *ctx->damage;
	int type = *ctx->type;
	XGLuaDmgCtx local = *ctx;
	local.damage = &damage;
	local.type = &type;
	local.generation = ++g_XGLuaDmgGeneration;
	if( !local.generation ) local.generation = ++g_XGLuaDmgGeneration;
	XGLuaDmgCtx *prev = g_pXGLuaDmgLive;
	g_pXGLuaDmgLive = &local;
	XGLua_HookBoolCtx hook = { "EntityTakeDamage", XGLua_PushDmgArgs, &local, 0 };
	const bool ok = XGLua_ProtectedCall( L, XGLua_HookRunBoolCore, &hook, XGLua_ReportError, "hook.Run(\"EntityTakeDamage\")" );
	g_pXGLuaDmgLive = prev;
	if( !ok ) return 0;
	*ctx->damage = damage;
	*ctx->type = type;
	return hook.result == 1 ? 1 : 0;
}

struct XGLuaEntPairCtx { edict_t *a; edict_t *b; };

static int XGLua_PushEntPair( lua_State *L, void *ud )
{
	XGLuaEntPairCtx *p = (XGLuaEntPairCtx *)ud;
	XGLua_PushEntity( L, p->a );
	if( p->b )
		XGLua_PushEntity( L, p->b );
	else
		lua_pushnil( L );
	return 2;
}

// PlayerShouldTakeDamage(ply, attacker): deny-style - only a strict Lua
// false exempts the player; everything else keeps the native gamerules
// decision and damage flow.
static bool XGLua_PlayerShouldTakeDamageImpl( lua_State *L, edict_t *player, edict_t *attacker )
{
	if( !L || !player )
		return false;
	XGLuaEntPairCtx ctx = { player, attacker };
	return XGLua_HookDenyBool( L, "PlayerShouldTakeDamage", XGLua_PushEntPair, &ctx, XGLua_ReportError );
}

// PlayerUse(ply, entity): deny-style - only a strict Lua false blocks this
// one interaction; the caller then skips the Use/tank/train interaction it
// was about to perform.
static bool XGLua_PlayerUseImpl( lua_State *L, edict_t *player, edict_t *target )
{
	if( !L || !player || !target )
		return false;
	XGLuaEntPairCtx ctx = { player, target };
	return XGLua_HookDenyBool( L, "PlayerUse", XGLua_PushEntPair, &ctx, XGLua_ReportError );
}

#endif // XGLUA_DAMAGE_H
