// Regression for WeaponBase special reload & BodyFromClip hooks (M249 Phase 2).
// Links only against LuaJIT; no engine binaries required.

#define HSPRITE XGLUA_TEST_WIN_HSPRITE
#include "test_framework.h"
#undef HSPRITE

static double g_now = 10.0;
static int g_clip = 0;
static int g_reserve = 200;
static int g_body = 0;
static bool g_inReload = false;
static double g_reloadEnd = 0.0;
static int g_lastAnim = -1;
static int g_lastAnimBody = -1;
static int g_animCalls = 0;

static int stub_CurTime( lua_State *L )
{
	// Simulate C++ ItemPostFrame completing reload when timer expires
	if( g_inReload && g_now >= g_reloadEnd )
	{
		int need = 50 - g_clip;
		int take = need < g_reserve ? need : g_reserve;
		g_clip += take;
		g_reserve -= take;
		g_inReload = false;
	}
	lua_pushnumber( L, g_now );
	return 1;
}

static int stub_IsValid( lua_State *L ) { lua_pushboolean( L, lua_istable( L, 1 ) ? 1 : 0 ); return 1; }

static bool SetupReloadTest( lua_State *L )
{
	g_now = 10.0; g_clip = 0; g_reserve = 200; g_body = 0;
	g_inReload = false; g_reloadEnd = 0.0;
	g_lastAnim = -1; g_lastAnimBody = -1; g_animCalls = 0;

	lua_pushcfunction( L, stub_CurTime ); lua_setglobal( L, "CurTime" );
	lua_pushcfunction( L, stub_IsValid ); lua_setglobal( L, "IsValid" );

	if( luaL_loadfile( L, "../../lua/includes/modules/weapons.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: weapons.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}

	const char *setupScript =
		"MockOwner = {\n"
		"  IsPlayer = function() return true end,\n"
		"  GetAmmoCount = function() return TestGetReserve() end\n"
		"}\n"
		"local M249 = {\n"
		"  Primary = { ClipSize = 50, DefaultClip = 50, Ammo = '556', Automatic = true },\n"
		"  SpecialReload = { OpenAnim = 3, OpenDelay = 1.33, CloseAnim = 4, CloseDelay = 1.0 },\n"
		"  IdleSeq = 0,\n"
		"  BodyFromClip = function( self, clip )\n"
		"    if clip <= 0 then return 8 end\n"
		"    if clip <= 8 then return 9 - clip end\n"
		"    return 0\n"
		"  end,\n"
		"  OnReloadComplete = function( self ) ReloadDoneCount = (ReloadDoneCount or 0) + 1 end\n"
		"}\n"
		"weapons.Register( M249, 'weapon_test_m249' )\n"
		"Wep = setmetatable( {}, { __index = weapons.Get( 'weapon_test_m249' ) } )\n"
		"Wep.GetOwner = function() return MockOwner end\n"
		"Wep.Clip1 = function() return TestGetClip() end\n"
		"Wep.SetBody = function( self, b ) TestSetBody( b ) end\n"
		"Wep.GetBody = function() return TestGetBody() end\n"
		"Wep.IsReloading = function() return TestIsReloading() end\n"
		"Wep.DefaultReload = function( self, clip, anim, delay, body ) return TestDefaultReload( clip, anim, delay, body ) end\n"
		"Wep.SendWeaponAnim = function( self, seq, body ) TestSendAnim( seq, body ) end\n"
		"Wep.SetNextPrimaryFire = function( self, t ) NextPri = t end\n"
		"Wep.SetNextSecondaryFire = function( self, t ) NextSec = t end\n"
		"Wep.SetNextAttack = function( self, d ) NextAtkDelay = d end\n"
		"Wep.SetWeaponIdleTime = function( self, t ) NextIdle = t end\n";

	lua_pushcfunction( L, []( lua_State *L ) -> int { lua_pushinteger( L, g_clip ); return 1; } );
	lua_setglobal( L, "TestGetClip" );
	lua_pushcfunction( L, []( lua_State *L ) -> int { lua_pushinteger( L, g_reserve ); return 1; } );
	lua_setglobal( L, "TestGetReserve" );
	lua_pushcfunction( L, []( lua_State *L ) -> int { g_body = (int)luaL_checkinteger( L, 1 ); return 0; } );
	lua_setglobal( L, "TestSetBody" );
	lua_pushcfunction( L, []( lua_State *L ) -> int { lua_pushinteger( L, g_body ); return 1; } );
	lua_setglobal( L, "TestGetBody" );
	lua_pushcfunction( L, []( lua_State *L ) -> int { stub_CurTime( L ); lua_pop( L, 1 ); lua_pushboolean( L, g_inReload ? 1 : 0 ); return 1; } );
	lua_setglobal( L, "TestIsReloading" );
	lua_pushcfunction( L, []( lua_State *L ) -> int {
		int anim = (int)luaL_checkinteger( L, 2 );
		double delay = luaL_checknumber( L, 3 );
		int body = (int)luaL_optinteger( L, 4, g_body );
		g_body = body;
		g_lastAnim = anim;
		g_lastAnimBody = body;
		++g_animCalls;
		g_inReload = true;
		g_reloadEnd = g_now + delay;
		lua_pushboolean( L, 1 );
		return 1;
	} );
	lua_setglobal( L, "TestDefaultReload" );
	lua_pushcfunction( L, []( lua_State *L ) -> int {
		g_lastAnim = (int)luaL_checkinteger( L, 1 );
		g_lastAnimBody = (int)luaL_optinteger( L, 2, g_body );
		++g_animCalls;
		return 0;
	} );
	lua_setglobal( L, "TestSendAnim" );

	return ::xtest::RunLua( L, setupScript, __FILE__, __LINE__ );
}

TEST(BodyFromClip_and_three_stage_reload_flow)
{
	// Verify BodyFromClip mapping across empty, partial belt (1..8), and full (>8)
	CHECK_LUA_INT( "Wep:GetBodyForClip(0)", 8, "empty belt body = 8" );
	CHECK_LUA_INT( "Wep:GetBodyForClip(1)", 8, "1 round left body = 8" );
	CHECK_LUA_INT( "Wep:GetBodyForClip(5)", 4, "5 rounds left body = 4" );
	CHECK_LUA_INT( "Wep:GetBodyForClip(8)", 1, "8 rounds left body = 1" );
	CHECK_LUA_INT( "Wep:GetBodyForClip(9)", 0, "9 rounds left body = 0" );
	CHECK_LUA_INT( "Wep:GetBodyForClip(50)", 0, "full belt body = 0" );

	// Stage 1: Trigger reload at clip = 0 -> open cover (anim 3, body 8)
	LUA( "Started = Wep:Reload()" );
	CHECK_LUA_BOOL( "Started", true, "Stage 1 reload started" );
	CHECK_EQ_INT( g_lastAnim, 3, "Stage 1 plays OpenAnim (3)" );
	CHECK_EQ_INT( g_lastAnimBody, 8, "Stage 1 keeps empty belt body (8)" );

	// Holding R mid-stage must not retrigger Stage 1
	g_now = 10.5;
	LUA( "Retrigger = Wep:Reload()" );
	CHECK_LUA_BOOL( "Retrigger", false, "Reload does not re-enter during Stage 1" );
	CHECK_EQ_INT( g_animCalls, 1, "No duplicate animation emitted mid-Stage 1" );

	// Stage 1 expires at 11.33 -> clip refilled to 50, Stage 2 plays CloseAnim (4) with body 0
	g_now = 11.35;
	LUA( "Wep:WeaponIdle()" );
	CHECK_EQ_INT( g_clip, 50, "Clip refilled to 50" );
	CHECK_EQ_INT( g_reserve, 150, "Reserve deducted by 50" );
	CHECK_EQ_INT( g_body, 0, "Belt body updated to 0 (full)" );
	CHECK_EQ_INT( g_lastAnim, 4, "Stage 2 plays CloseAnim (4)" );
	CHECK_EQ_INT( g_lastAnimBody, 0, "Stage 2 uses updated full belt body (0)" );

	// During Stage 2 (before 12.35), WeaponIdle must not interrupt CloseAnim with IdleSeq (0)
	g_now = 11.80;
	LUA( "Wep:WeaponIdle()" );
	CHECK_EQ_INT( g_lastAnim, 4, "Idle suppressed while Stage 2 close animation plays" );

	// Stage 2 completes at 12.35 -> OnReloadComplete fires and normal idle resumes
	g_now = 12.40;
	LUA( "Wep:WeaponIdle()" );
	CHECK_LUA_INT( "ReloadDoneCount", 1, "OnReloadComplete fired once" );
	CHECK_EQ_INT( g_lastAnim, 0, "Normal IdleSeq resumes after Stage 2" );
}

TEST(Holster_resets_special_reload_stage)
{
	g_now = 20.0; g_clip = 10; g_reserve = 100; g_inReload = false;
	LUA( "Wep:Reload(); Wep:Holster()" );
	CHECK_LUA_INT( "Wep._SpecialReloadStage", 0, "Holster clears _SpecialReloadStage" );
}

int main()
{
	xtest::Setup() = SetupReloadTest;
	return xtest::RunAll( "swep_special_reload" );
}
