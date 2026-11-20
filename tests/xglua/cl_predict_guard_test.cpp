// Regression for cl_dll/lua/xglua_cl_predict.h / xglua_cl_predict.cpp
// prediction boundary guards.

#define HSPRITE XGLUA_TEST_WIN_HSPRITE
#include "test_framework.h"
#undef HSPRITE
#include "lua/xglua_cl_predict.h"

// Minimal stubs so standalone host test links cleanly
void HUD_SendWeaponAnim( int iAnim, int body, int force )
{
	(void)iAnim; (void)body; (void)force;
}
void HUD_PlaySound( const char *szSound, float vol )
{
	(void)szSound; (void)vol;
}
void XGLua_CL_ReportError( lua_State *L, const char *context )
{
	(void)L; (void)context;
}

static bool SetupPredictTest( lua_State *L )
{
	std::printf( "  setup enter\n" );
	std::fflush( stdout );

	XGLua_CL_SetPredicting( false );
	XGLua_CL_RegisterPredictLibs( L );

	const char *script =
		"XGLUA_CL_ActiveSWEP = { Clip = 15, NextPrimaryFire = 0, NextSecondaryFire = 0 }\n"
		"local api = XGLUA_CL_PredictAPI\n"
		"PredWep = {\n"
		"  Clip1 = function(self) return api.Clip1(self) end,\n"
		"  SetClip1 = function(self, n) api.SetClip1(self, n) end,\n"
		"  SetNextPrimaryFire = function(self, t) api.SetNextPrimaryFire(self, t) end,\n"
		"  SendWeaponAnim = function(self, s) api.SendWeaponAnim(self, s) end,\n"
		"  TakeDamage = function(self, d) api.TakeDamage(self, d) end,\n"
		"  Remove = function(self) api.Remove(self) end\n"
		"}";

	if( !::xtest::RunLua( L, script, __FILE__, __LINE__ ) )
		return false;

	return true;
}

TEST(Predict_scope_allows_state_and_blocks_side_effects)
{
	{
		XGLuaCLPredictScope scope( L, true );
		CHECK_EQ_BOOL( XGLua_CL_IsPredicting(), true, "inside predict scope" );
		LUA( "PredWep:SetClip1(14); PredWep:SetNextPrimaryFire(1.25); PredWep:SendWeaponAnim(5)" );
		CHECK_LUA_INT( "PredWep:Clip1()", 14, "predicted clip updated" );
		CHECK_LUA_FLT( "XGLUA_CL_ActiveSWEP.NextPrimaryFire", 1.25, "predicted fire timer updated" );
		CHECK_LUA_INT( "XGLUA_CL_ActiveSWEP.AnimSeq", 5, "predicted anim seq recorded" );

		CHECK_LUA_ERROR( "PredWep:TakeDamage(50)", "not allowed inside prediction", "blocks TakeDamage during predict" );
		CHECK_LUA_ERROR( "PredWep:Remove()", "not allowed inside prediction", "blocks Remove during predict" );
		CHECK_LUA_ERROR( "ents.Create('monster_barney')", "not allowed inside prediction", "blocks ents.Create during predict" );
	}
	CHECK_EQ_BOOL( XGLua_CL_IsPredicting(), false, "predict scope exited cleanly" );
}

int main()
{
	xtest::Setup() = SetupPredictTest;
	return xtest::RunAll( "cl_predict_guard" );
}