/***
*
*	XGLua — Client-side SWEP P2 prediction & restricted API guard (M7 7A)
*
****/

#include "lua/xglua_cl_predict.h"
#include "lua/xglua_weapon_anim.h"

// Engine-side callbacks are injected rather than included: this TU stays
// free of hud.h/vgui so host tests can link it directly.
void XGLua_CL_ReportError( lua_State *L, const char *context );	// cl_vm.cpp
void HUD_SendWeaponAnim( int iAnim, int body, int force );	// com_weapons
void HUD_PlaySound( const char *sound, float volume );	// com_weapons

static bool g_CLPredicting = false;

bool XGLua_CL_IsPredicting( void )
{
	return g_CLPredicting;
}

void XGLua_CL_SetPredicting( bool predicting )
{
	g_CLPredicting = predicting;
}

XGLuaCLPredictScope::XGLuaCLPredictScope( lua_State *state, bool enable )
	: L( state ), prevFlag( g_CLPredicting ), active( enable )
{
	if( active )
		g_CLPredicting = true;
}

XGLuaCLPredictScope::~XGLuaCLPredictScope()
{
	if( active )
		g_CLPredicting = prevFlag;
}

//===========================================================================
// Client-side SWEP registry lookup & protected call
//===========================================================================

static bool CL_PushSWEPMethod( lua_State *L, const char *classname, const char *method )
{
	if( !classname || !classname[0] || !method || !method[0] )
		return false;

	lua_getglobal( L, "weapons" );
	if( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); return false; }

	lua_getfield( L, -1, "GetStored" );
	if( !lua_isfunction( L, -1 ) ) { lua_pop( L, 2 ); return false; }

	lua_pushstring( L, classname );
	if( lua_pcall( L, 1, 1, 0 ) != 0 )
	{
		lua_pop( L, 2 );
		return false;
	}

	if( !lua_istable( L, -1 ) ) { lua_pop( L, 2 ); return false; }

	lua_getfield( L, -1, method );
	if( !lua_isfunction( L, -1 ) ) { lua_pop( L, 3 ); return false; }

	lua_remove( L, -2 ); // swep table
	lua_remove( L, -2 ); // weapons table
	return true;
}

struct XGLua_CLSWEPCallCtx
{
	const char *classname;
	const char *method;
};

static int XGLua_CLSWEPCallCore( lua_State *L, void *ud )
{
	XGLua_CLSWEPCallCtx *ctx = (XGLua_CLSWEPCallCtx *)ud;
	if( !CL_PushSWEPMethod( L, ctx->classname, ctx->method ) )
		return 0;

	// Pass active client SWEP context table so Lua SWEP methods can read/write
	// predicted clip/timers cleanly without server-only entity userdata.
	lua_getglobal( L, "XGLUA_CL_ActiveSWEP" );
	if( lua_pcall( L, 1, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

void XGLua_CL_CallSWEPMethod( lua_State *L, const char *classname, const char *method )
{
	if( !L || !classname || !method )
		return;
	XGLua_CLSWEPCallCtx ctx = { classname, method };
	char label[128];
	snprintf( label, sizeof( label ), "CL SWEP:%s", method );
	XGLua_ProtectedCall( L, XGLua_CLSWEPCallCore, &ctx, XGLua_CL_ReportError, label );
}

//===========================================================================
// Restricted predictable API subset exposed to client Lua
//===========================================================================

static void CL_GetActiveSWEPTable( lua_State *L )
{
	lua_getglobal( L, "XGLUA_CL_ActiveSWEP" );
	if( !lua_istable( L, -1 ) )
		luaL_error( L, "No active client SWEP prediction context" );
	return;
}

static int clswep_Clip1( lua_State *L )
{
	CL_GetActiveSWEPTable( L );
	lua_getfield( L, -1, "Clip" );
	lua_Number v = luaL_optnumber( L, -1, 0 );
	lua_pop( L, 2 );
	lua_pushnumber( L, v );
	return 1;
}

static int clswep_SetClip1( lua_State *L )
{
	lua_Number val = luaL_checknumber( L, 2 );
	CL_GetActiveSWEPTable( L );
	lua_pushnumber( L, val );
	lua_setfield( L, -2, "Clip" );
	lua_pop( L, 1 );
	return 0;
}

static int clswep_SetNextPrimaryFire( lua_State *L )
{
	lua_Number t = luaL_checknumber( L, 2 );
	CL_GetActiveSWEPTable( L );
	lua_pushnumber( L, t );
	lua_setfield( L, -2, "NextPrimaryFire" );
	lua_pop( L, 1 );
	return 0;
}

static int clswep_GetNextPrimaryFire( lua_State *L )
{
	CL_GetActiveSWEPTable( L );
	lua_getfield( L, -1, "NextPrimaryFire" );
	lua_Number t = luaL_optnumber( L, -1, 0 );
	lua_pop( L, 2 );
	lua_pushnumber( L, t );
	return 1;
}

static int clswep_SetNextSecondaryFire( lua_State *L )
{
	lua_Number t = luaL_checknumber( L, 2 );
	CL_GetActiveSWEPTable( L );
	lua_pushnumber( L, t );
	lua_setfield( L, -2, "NextSecondaryFire" );
	lua_pop( L, 1 );
	return 0;
}

static int clswep_GetNextSecondaryFire( lua_State *L )
{
	CL_GetActiveSWEPTable( L );
	lua_getfield( L, -1, "NextSecondaryFire" );
	lua_Number t = luaL_optnumber( L, -1, 0 );
	lua_pop( L, 2 );
	lua_pushnumber( L, t );
	return 1;
}

static int clswep_SendWeaponAnim( lua_State *L )
{
	int seq = (int)luaL_checknumber( L, 2 );
	bool explicitBody = !lua_isnoneornil( L, 3 );
	int body = explicitBody ? XGLua_CheckWeaponBody( L, 3 ) : 0;
	CL_GetActiveSWEPTable( L );
	if( !explicitBody )
	{
		lua_getfield( L, -1, "AnimBody" );
		if( !lua_isnil( L, -1 ) ) body = XGLua_CheckWeaponBody( L, -1 );
		lua_pop( L, 1 );
	}
	lua_pushinteger( L, seq );
	lua_setfield( L, -2, "AnimSeq" );
	lua_pushinteger( L, body );
	lua_setfield( L, -2, "AnimBody" );
	lua_pop( L, 1 );
	HUD_SendWeaponAnim( seq, body, 1 );
	return 0;
}

static int clswep_PlayEmptySound( lua_State *L )
{
	HUD_PlaySound( "weapons/357_cock1.wav", 0.8f );
	lua_pushboolean( L, 1 );
	return 1;
}

static int clswep_EmitSound( lua_State *L )
{
	const char *snd = luaL_checkstring( L, 2 );
	float vol = (float)luaL_optnumber( L, 3, 1.0 );
	HUD_PlaySound( snd, vol );
	return 0;
}

// Explicitly blocked during prediction: server-authoritative / side-effectful calls
static int clswep_TakeDamage( lua_State *L )
{
	XGLua_CL_GuardNonPredictable( L, "TakeDamage" );
	return 0;
}

static int clswep_Remove( lua_State *L )
{
	XGLua_CL_GuardNonPredictable( L, "Remove" );
	return 0;
}

static int clswep_CreateEnt( lua_State *L )
{
	XGLua_CL_GuardNonPredictable( L, "ents.Create" );
	return 0;
}

void XGLua_CL_RegisterPredictLibs( lua_State *L )
{
	lua_newtable( L );
	static const luaL_Reg pred_api[] =
	{
		{ "Clip1",                clswep_Clip1 },
		{ "SetClip1",             clswep_SetClip1 },
		{ "SetNextPrimaryFire",   clswep_SetNextPrimaryFire },
		{ "GetNextPrimaryFire",   clswep_GetNextPrimaryFire },
		{ "SetNextSecondaryFire", clswep_SetNextSecondaryFire },
		{ "GetNextSecondaryFire", clswep_GetNextSecondaryFire },
		{ "SendWeaponAnim",       clswep_SendWeaponAnim },
		{ "PlayEmptySound",       clswep_PlayEmptySound },
		{ "EmitSound",            clswep_EmitSound },
		{ "TakeDamage",           clswep_TakeDamage },
		{ "Remove",               clswep_Remove },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = pred_api; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "XGLUA_CL_PredictAPI" );

	lua_newtable( L );
	lua_pushcfunction( L, clswep_CreateEnt );
	lua_setfield( L, -2, "Create" );
	lua_setglobal( L, "ents" );
}
