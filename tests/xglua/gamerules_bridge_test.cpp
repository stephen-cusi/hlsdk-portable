// Regression for dlls/lua/xglua_gamerules_bridge.h:
// XGLua_HookRunTriBool / XGLua_HookRunFloat / XGLua_HookRunInt /
// XGLua_HookRunEntity against the real lua/includes/modules/hook.lua.
// Run from tests/xglua so hook.lua's relative path resolves.

#include "test_framework.h"
#include "xglua_gamerules_bridge.h"

static int reportedErrors;
static char lastError[256];

static void TestReportError( lua_State *L, const char *context )
{
	++reportedErrors;
	const char *msg = XGLua_ErrorText( L, -1 );
	snprintf( lastError, sizeof( lastError ), "%s", msg ? msg : "?" );
	(void)context;
	lua_pop( L, 1 );
}

//---------------------------------------------------------------------------
// Stub entity userdata + extractor (mirrors XGLua_TryEntity: never raises)
//---------------------------------------------------------------------------
struct StubEnt { int id; bool valid; };
static StubEnt g_ents[3];
#define STUB_ENT_META "XGLua_GRTestEnt"

static void PushStubEnt( lua_State *L, StubEnt *e )
{
	StubEnt **ud = (StubEnt **)lua_newuserdata( L, sizeof( StubEnt * ) );
	*ud = e;
	luaL_getmetatable( L, STUB_ENT_META );
	lua_setmetatable( L, -2 );
}

static void *ExtractStubEnt( lua_State *L, int idx )
{
	if( lua_type( L, idx ) != LUA_TUSERDATA || !lua_getmetatable( L, idx ) )
		return NULL;
	luaL_getmetatable( L, STUB_ENT_META );
	bool match = lua_rawequal( L, -1, -2 ) != 0;
	lua_pop( L, 2 );
	if( !match )
		return NULL;
	StubEnt *e = *(StubEnt **)lua_touserdata( L, idx );
	return ( e && e->valid ) ? (void *)e : NULL;
}

static int PushOneStubEnt( lua_State *L, void *ctx )
{
	StubEnt *e = (StubEnt *)ctx;
	if( e ) PushStubEnt( L, e );
	else lua_pushnil( L );
	return 1;
}

//---------------------------------------------------------------------------
// Lua-facing dispatch helpers
//---------------------------------------------------------------------------
static int fx_tribool( lua_State *L )
{
	const char *ev = luaL_checkstring( L, 1 );
	int slot = (int)luaL_optinteger( L, 2, -1 );
	StubEnt *arg = ( slot >= 0 && slot < 3 ) ? &g_ents[slot] : NULL;
	int res = XGLua_HookRunTriBool( L, ev, arg ? PushOneStubEnt : NULL, arg, TestReportError );
	lua_pushinteger( L, res );
	return 1;
}

static int fx_float( lua_State *L )
{
	const char *ev = luaL_checkstring( L, 1 );
	float out = -999.5f; // sentinel: must stay untouched on false
	bool ok = XGLua_HookRunFloat( L, ev, PushOneStubEnt, &g_ents[0], &out, TestReportError );
	lua_pushboolean( L, ok );
	lua_pushnumber( L, (double)out );
	return 2;
}

static int fx_int( lua_State *L )
{
	const char *ev = luaL_checkstring( L, 1 );
	int out = -777; // sentinel: must stay untouched on false
	bool ok = XGLua_HookRunInt( L, ev, PushOneStubEnt, &g_ents[0], &out, TestReportError );
	lua_pushboolean( L, ok );
	lua_pushinteger( L, out );
	return 2;
}

static int fx_entity( lua_State *L )
{
	const char *ev = luaL_checkstring( L, 1 );
	StubEnt *res = (StubEnt *)XGLua_HookRunEntity( L, ev, PushOneStubEnt, &g_ents[0],
		ExtractStubEnt, TestReportError );
	if( res )
		lua_pushinteger( L, res->id );
	else
		lua_pushnil( L );
	return 1;
}

static int fx_ent_global( lua_State *L )
{
	int slot = (int)luaL_checkinteger( L, 1 );
	if( slot < 0 || slot >= 3 ) return luaL_argerror( L, 1, "bad slot" );
	PushStubEnt( L, &g_ents[slot] );
	return 1;
}

static int fx_invalidate( lua_State *L )
{
	int slot = (int)luaL_checkinteger( L, 1 );
	if( slot >= 0 && slot < 3 ) g_ents[slot].valid = false;
	return 0;
}

static int fx_errors( lua_State *L )
{
	lua_pushinteger( L, reportedErrors );
	lua_pushstring( L, lastError );
	return 2;
}

static bool SetupGRBridge( lua_State *L )
{
	reportedErrors = 0;
	lastError[0] = '\0';
	for( int i = 0; i < 3; ++i )
	{
		g_ents[i].id = 100 + i;
		g_ents[i].valid = true;
	}
	if( luaL_loadfile( L, "../../lua/includes/modules/hook.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: hook.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	luaL_newmetatable( L, STUB_ENT_META );
	lua_pop( L, 1 );

	static const luaL_Reg helpers[] = {
		{ "runTriBool", fx_tribool },
		{ "runFloat", fx_float },
		{ "runInt", fx_int },
		{ "runEntity", fx_entity },
		{ "stubEnt", fx_ent_global },
		{ "invalidate", fx_invalidate },
		{ "errors", fx_errors },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = helpers; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setglobal( L, f->name );
	}
	return true;
}

//---------------------------------------------------------------------------
// TriBool
//---------------------------------------------------------------------------
TEST(GR_tribool_matrix_and_priority)
{
	CHECK_LUA_INT( "runTriBool('AllowMonsters')", 0, "TriBool: no handlers -> 0" );

	static const char *rets[] = { "true", "false", "nil", "0", "1", "'yes'", "{}", NULL };
	static const int expected[] = { 1, 2, 0, 0, 0, 0, 0 };
	for( int i = 0; rets[i]; ++i )
	{
		char code[160], desc[96];
		snprintf( code, sizeof( code ), "hook.Add('AllowMonsters', 't', function() return %s end)", rets[i] );
		LUA( code );
		snprintf( desc, sizeof( desc ), "TriBool: return %s -> %d", rets[i], expected[i] );
		CHECK_LUA_INT( "runTriBool('AllowMonsters')", expected[i], desc );
		LUA( "hook.Remove('AllowMonsters', 't')" );
	}

	LUA( "GAMEMODE = { AllowMonsters = function() return false end }" );
	CHECK_LUA_INT( "runTriBool('AllowMonsters')", 2, "TriBool: GM fallback false -> 2" );
	LUA( "hook.Add('AllowMonsters', 'ov', function() return true end)" );
	CHECK_LUA_INT( "runTriBool('AllowMonsters')", 1, "TriBool: hook true beats GM false" );
	LUA( "hook.Remove('AllowMonsters', 'ov')\n"
		"GAMEMODE = { AllowMonsters = function() error('tribool boom') end }" );
	CHECK_LUA_INT( "runTriBool('AllowMonsters')", 0, "TriBool: GM error -> 0" );
	CHECK_LUA_INT( "select(1, errors())", 1, "TriBool: GM error reported once" );
}

//---------------------------------------------------------------------------
// Float
//---------------------------------------------------------------------------
TEST(GR_float_validation_and_fallback)
{
	LUA( "Ok0, Val0 = runFloat('GetFallDamage')" );
	CHECK_LUA_BOOL( "Ok0", false, "Float: no handlers -> false" );
	CHECK_LUA_FLT( "Val0", -999.5, "Float: out untouched when no handlers" );

	LUA( "hook.Add('GetFallDamage', 'f', function() return 12.5 end)\n"
		"Ok1, Val1 = runFloat('GetFallDamage')" );
	CHECK_LUA_BOOL( "Ok1", true, "Float: finite number -> true" );
	CHECK_LUA_FLT( "Val1", 12.5, "Float: value written through" );

	static const char *bad[] = { "0/0", "1/0", "-1/0", "1e300", "'12.5'", "true", "nil", "{}", NULL };
	for( int i = 0; bad[i]; ++i )
	{
		char code[160], desc[96];
		snprintf( code, sizeof( code ),
			"hook.Add('GetFallDamage', 'f', function() return %s end)\nOkB, ValB = runFloat('GetFallDamage')",
			bad[i] );
		LUA( code );
		snprintf( desc, sizeof( desc ), "Float: invalid %s rejected", bad[i] );
		CHECK_LUA_BOOL( "OkB", false, desc );
		CHECK_LUA_FLT( "ValB", -999.5, "Float: out untouched on rejection" );
	}

	LUA( "hook.Remove('GetFallDamage', 'f')\n"
		"GAMEMODE = { GetFallDamage = function() error('float boom') end }\n"
		"OkE, ValE = runFloat('GetFallDamage')" );
	CHECK_LUA_BOOL( "OkE", false, "Float: GM error -> false" );
	CHECK_LUA_FLT( "ValE", -999.5, "Float: out untouched on error" );
	CHECK_LUA_INT( "select(1, errors())", 1, "Float: GM error reported once" );
}

//---------------------------------------------------------------------------
// Int
//---------------------------------------------------------------------------
TEST(GR_int_validation_and_bounds)
{
	LUA( "Ok0, Val0 = runInt('GetPointsForKill')" );
	CHECK_LUA_BOOL( "Ok0", false, "Int: no handlers -> false" );
	CHECK_LUA_INT( "Val0", -777, "Int: out untouched when no handlers" );

	LUA( "hook.Add('GetPointsForKill', 'i', function() return -2147483648 end)\n"
		"OkMin, ValMin = runInt('GetPointsForKill')" );
	CHECK_LUA_BOOL( "OkMin", true, "Int: INT_MIN accepted" );
	CHECK_LUA_INT( "ValMin", -2147483648LL, "Int: INT_MIN value preserved" );

	LUA( "hook.Add('GetPointsForKill', 'i', function() return 2147483647 end)\n"
		"OkMax, ValMax = runInt('GetPointsForKill')" );
	CHECK_LUA_BOOL( "OkMax", true, "Int: INT_MAX accepted" );
	CHECK_LUA_INT( "ValMax", 2147483647LL, "Int: INT_MAX value preserved" );

	static const char *bad[] = { "1.5", "-0.25", "2147483648", "-2147483649", "0/0", "1/0", "'5'", "false", "nil", NULL };
	for( int i = 0; bad[i]; ++i )
	{
		char code[160], desc[96];
		snprintf( code, sizeof( code ),
			"hook.Add('GetPointsForKill', 'i', function() return %s end)\nOkB, ValB = runInt('GetPointsForKill')",
			bad[i] );
		LUA( code );
		snprintf( desc, sizeof( desc ), "Int: invalid %s rejected", bad[i] );
		CHECK_LUA_BOOL( "OkB", false, desc );
		CHECK_LUA_INT( "ValB", -777, "Int: out untouched on rejection" );
	}

	LUA( "hook.Remove('GetPointsForKill', 'i')\n"
		"GAMEMODE = { GetPointsForKill = function() error('int boom') end }\n"
		"OkE, ValE = runInt('GetPointsForKill')" );
	CHECK_LUA_BOOL( "OkE", false, "Int: GM error -> false" );
	CHECK_LUA_INT( "ValE", -777, "Int: out untouched on error" );
	CHECK_LUA_INT( "select(1, errors())", 1, "Int: GM error reported once" );
}

//---------------------------------------------------------------------------
// Entity
//---------------------------------------------------------------------------
TEST(GR_entity_extraction_and_guards)
{
	CHECK_LUA_NIL( "runEntity('PlayerSelectSpawn')", "Entity: no handlers -> nil" );

	LUA( "hook.Add('PlayerSelectSpawn', 'e', function(ply) return stubEnt(2) end)" );
	CHECK_LUA_INT( "runEntity('PlayerSelectSpawn')", 102, "Entity: valid handle extracted" );

	LUA( "invalidate(2)" );
	CHECK_LUA_NIL( "runEntity('PlayerSelectSpawn')", "Entity: invalidated handle -> nil" );

	LUA( "hook.Add('PlayerSelectSpawn', 'e', function() return {} end)" );
	CHECK_LUA_NIL( "runEntity('PlayerSelectSpawn')", "Entity: non-entity table -> nil" );

	LUA( "hook.Remove('PlayerSelectSpawn', 'e')\n"
		"GAMEMODE = { PlayerSelectSpawn = function() error('ent boom') end }" );
	CHECK_LUA_NIL( "runEntity('PlayerSelectSpawn')", "Entity: GM error -> nil" );
	CHECK_LUA_INT( "select(1, errors())", 1, "Entity: GM error reported once" );
}

//---------------------------------------------------------------------------
// NULL / missing hook table guards
//---------------------------------------------------------------------------
TEST(GR_null_and_missing_hook_guards)
{
	float f = 1.0f;
	int iv = 1;
	CHECK_EQ_INT( XGLua_HookRunTriBool( NULL, "X", NULL, NULL, TestReportError ), 0, "TriBool: NULL state" );
	CHECK_EQ_INT( XGLua_HookRunTriBool( L, NULL, NULL, NULL, TestReportError ), 0, "TriBool: NULL event" );
	CHECK_EQ_BOOL( XGLua_HookRunFloat( NULL, "X", NULL, NULL, &f, TestReportError ), false, "Float: NULL state" );
	CHECK_EQ_BOOL( XGLua_HookRunFloat( L, "X", NULL, NULL, NULL, TestReportError ), false, "Float: NULL out" );
	CHECK_EQ_BOOL( XGLua_HookRunInt( NULL, "X", NULL, NULL, &iv, TestReportError ), false, "Int: NULL state" );
	CHECK_EQ_BOOL( XGLua_HookRunInt( L, "X", NULL, NULL, NULL, TestReportError ), false, "Int: NULL out" );
	CHECK_EQ_PTR( XGLua_HookRunEntity( NULL, "X", NULL, NULL, ExtractStubEnt, TestReportError ), NULL, "Entity: NULL state" );
	CHECK_EQ_PTR( XGLua_HookRunEntity( L, "X", NULL, NULL, NULL, TestReportError ), NULL, "Entity: NULL extractor" );

	LUA( "hook = nil" );
	CHECK_LUA_INT( "runTriBool('AllowMonsters')", 0, "missing hook global: TriBool silent 0" );
	CHECK_LUA_BOOL( "select(1, runFloat('GetFallDamage'))", false, "missing hook global: Float silent false" );
	CHECK_LUA_BOOL( "select(1, runInt('GetPointsForKill'))", false, "missing hook global: Int silent false" );
	CHECK_LUA_NIL( "runEntity('PlayerSelectSpawn')", "missing hook global: Entity silent nil" );
	CHECK_EQ_INT( reportedErrors, 0, "guards: no errors reported" );
}

int main()
{
	xtest::Setup() = SetupGRBridge;
	return xtest::RunAll( "gamerules_bridge" );
}
