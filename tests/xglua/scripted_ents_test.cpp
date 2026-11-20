// Regression for scripted_ents.lua and dlls/lua/xglua_sent_bridge.h:
// registration, Base inheritance & cycle protection, Abstract isolation,
// Type validation, C++ registry lookup, protected ENT:* dispatch, and
// tri-state boolean dispatch. Run from tests/xglua so relative paths resolve.

#include "test_framework.h"
#include "xglua_sent_bridge.h"

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

static int fx_store( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TTABLE );
	lua_pushboolean( L, XGLua_StoreSENTTable( L, 2, cls ) );
	return 1;
}

static int fx_is_lua_ent( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	lua_pushboolean( L, XGLua_IsLuaEntityImpl( L, cls, TestReportError ) );
	return 1;
}

static int fx_is_abstract( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	lua_pushboolean( L, XGLua_SENTIsAbstract( L, 1 ) );
	return 1;
}

static int fx_read_type( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	char buf[32] = { 0 };
	bool ok = XGLua_ReadSENTType( L, 1, buf, sizeof( buf ) );
	lua_pushboolean( L, ok );
	lua_pushstring( L, buf );
	return 2;
}

struct PushSelfArg { int selfRef; int extraInt; bool hasExtra; };
static int PushSelfAndOptionalInt( lua_State *L, void *ud )
{
	PushSelfArg *a = (PushSelfArg *)ud;
	lua_rawgeti( L, LUA_REGISTRYINDEX, a->selfRef );
	if( a->hasExtra )
	{
		lua_pushinteger( L, a->extraInt );
		return 2;
	}
	return 1;
}

static int fx_call_void( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	const char *method = luaL_checkstring( L, 2 );
	lua_pushvalue( L, 3 );
	int ref = luaL_ref( L, LUA_REGISTRYINDEX );
	PushSelfArg arg = { ref, 0, false };
	if( lua_gettop( L ) >= 4 )
	{
		arg.extraInt = (int)luaL_checkinteger( L, 4 );
		arg.hasExtra = true;
	}
	XGLua_CallSENTMethod( L, cls, method, PushSelfAndOptionalInt, &arg, TestReportError, "ENT:test" );
	luaL_unref( L, LUA_REGISTRYINDEX, ref );
	return 0;
}

static int fx_call_bool( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	const char *method = luaL_checkstring( L, 2 );
	lua_pushvalue( L, 3 );
	int ref = luaL_ref( L, LUA_REGISTRYINDEX );
	PushSelfArg arg = { ref, 0, false };
	int res = XGLua_CallSENTBoolMethod( L, cls, method, PushSelfAndOptionalInt, &arg, TestReportError, "ENT:testbool" );
	luaL_unref( L, LUA_REGISTRYINDEX, ref );
	lua_pushinteger( L, res );
	return 1;
}

static int fx_errors( lua_State *L )
{
	lua_pushinteger( L, reportedErrors );
	lua_pushstring( L, lastError );
	return 2;
}

static bool SetupSENT( lua_State *L )
{
	reportedErrors = 0;
	lastError[0] = '\0';
	if( luaL_loadfile( L, "../../lua/includes/modules/scripted_ents.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: scripted_ents.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	static const luaL_Reg helpers[] = {
		{ "storeSENT", fx_store },
		{ "isLuaEnt", fx_is_lua_ent },
		{ "isAbstract", fx_is_abstract },
		{ "readType", fx_read_type },
		{ "callVoid", fx_call_void },
		{ "callBool", fx_call_bool },
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

TEST(SENT_register_and_lookup)
{
	LUA( "T = { PrintName = 'Medkit', Charge = 25 }\n"
		"R = scripted_ents.Register( T, 'XGL_Medkit' )" );
	CHECK_LUA_BOOL( "rawequal(R, T)", true, "Register: returns the registered table" );
	CHECK_LUA_STR( "T.ClassName", "xgl_medkit", "Register: ClassName normalized to lowercase" );
	CHECK_LUA_STR( "T.Base", "base_entity", "Register: defaults Base to base_entity" );
	CHECK_LUA_BOOL( "rawequal(scripted_ents.Get('XGL_MEDKIT'), T)", true, "Get: case-insensitive lookup" );
	CHECK_LUA_STR( "scripted_ents.GetType('xgl_medkit')", "anim", "GetType: inherited default type anim" );
	CHECK_LUA_BOOL( "#scripted_ents.GetList() >= 4", true, "GetList: includes builtins + registered entity" );
}

TEST(SENT_inheritance_and_cycle_safety)
{
	// Register child before parent to verify order-independent Base resolution.
	LUA( "Child = { Base = 'my_parent', Own = 1 }\n"
		"scripted_ents.Register( Child, 'my_child' )\n"
		"Parent = { Base = 'base_point', Shared = 99, Own = 2 }\n"
		"scripted_ents.Register( Parent, 'my_parent' )" );
	CHECK_LUA_INT( "Child.Own", 1, "inheritance: child field shadows parent" );
	CHECK_LUA_INT( "Child.Shared", 99, "inheritance: resolves parent field" );
	CHECK_LUA_STR( "Child.Type", "point", "inheritance: resolves grandparent Type" );
	CHECK_LUA_BOOL( "rawequal(Child.BaseClass, Parent)", true, "inheritance: BaseClass points to parent" );

	// Cyclic Base chain must return nil instead of overflowing the stack.
	LUA( "A = { Base = 'cyc_b' }; B = { Base = 'cyc_a' }\n"
		"scripted_ents.Register( A, 'cyc_a' ); scripted_ents.Register( B, 'cyc_b' )" );
	CHECK_LUA_NIL( "A.MissingKey", "cycle: cyclic Base chain terminates cleanly with nil" );
}

TEST(SENT_reject_custom_metatable)
{
	LUA( "Custom = setmetatable( {}, {} )\n"
		"Res = scripted_ents.Register( Custom, 'xgl_custom_mt' )" );
	CHECK_LUA_NIL( "Res", "Register: rejects table with custom metatable" );
	CHECK_LUA_NIL( "scripted_ents.Get('xgl_custom_mt')", "Register: rejected table not stored" );
}

TEST(SENT_abstract_isolation_and_type_validation)
{
	LUA( "Child = {}; scripted_ents.Register( Child, 'xgl_concrete' )" );
	CHECK_LUA_BOOL( "isAbstract(scripted_ents.Get('base_entity'))", true, "Abstract: base_entity is abstract" );
	CHECK_LUA_BOOL( "isAbstract(Child)", false, "Abstract: concrete child does not inherit Abstract=true" );

	LUA( "OkAnim, TAnim = readType(Child)\n"
		"Bad = { Type = 'invalid_type' }; scripted_ents.Register( Bad, 'xgl_bad_type' )\n"
		"OkBad, TBad = readType(Bad)\n"
		"Pt = { Base = 'base_point' }; scripted_ents.Register( Pt, 'xgl_pt' )\n"
		"OkPt, TPt = readType(Pt)" );
	CHECK_LUA_BOOL( "OkAnim", true, "ReadSENTType: default anim accepted" );
	CHECK_LUA_STR( "TAnim", "anim", "ReadSENTType: default anim string" );
	CHECK_LUA_BOOL( "OkBad", false, "ReadSENTType: unknown type rejected" );
	CHECK_LUA_BOOL( "OkPt", true, "ReadSENTType: inherited point accepted" );
	CHECK_LUA_STR( "TPt", "point", "ReadSENTType: inherited point string" );
}

TEST(SENT_protected_dispatch_and_errors)
{
	LUA( "Base = { Base = 'base_entity', Ping = function(self, v) LastPing = self.id + v end }\n"
		"scripted_ents.Register( Base, 'xgl_base_disp' )\n"
		"Sub = { Base = 'xgl_base_disp', Boom = function() error('kaboom') end }\n"
		"scripted_ents.Register( Sub, 'xgl_sub_disp' )\n"
		"storeSENT( 'xgl_sub_disp', Sub )" );

	CHECK_LUA_BOOL( "isLuaEnt('xgl_sub_disp')", true, "IsLuaEntity: registered class found" );
	CHECK_LUA_BOOL( "isLuaEnt('nosuch_ent')", false, "IsLuaEntity: unknown class false" );

	LUA( "callVoid( 'xgl_sub_disp', 'Ping', { id = 10 }, 5 )" );
	CHECK_LUA_INT( "LastPing", 15, "CallSENTMethod: inherited method dispatched with (self, arg)" );

	LUA( "callVoid( 'xgl_sub_disp', 'NoSuchMethod', { id = 1 } )" );
	CHECK_LUA_INT( "select(1, errors())", 0, "CallSENTMethod: missing method is a silent no-op" );

	LUA( "callVoid( 'xgl_sub_disp', 'Boom', { id = 1 } )" );
	CHECK_LUA_INT( "select(1, errors())", 1, "CallSENTMethod: error reported once and contained" );
	CHECK_LUA_BOOL( "select(2, errors()):find('kaboom', 1, true) ~= nil", true, "CallSENTMethod: error message preserved" );
}

TEST(SENT_tristate_bool_dispatch)
{
	LUA( "T = {\n"
		"	RetTrue = function() return true end,\n"
		"	RetFalse = function() return false end,\n"
		"	RetNum = function() return 1 end,\n"
		"	RetErr = function() error('bool err') end\n"
		"}\n"
		"scripted_ents.Register( T, 'xgl_bool_ent' )\n"
		"storeSENT( 'xgl_bool_ent', T )" );
	CHECK_LUA_INT( "callBool('xgl_bool_ent', 'RetTrue', {})", 1, "CallSENTBool: strict true -> 1" );
	CHECK_LUA_INT( "callBool('xgl_bool_ent', 'RetFalse', {})", 2, "CallSENTBool: strict false -> 2" );
	CHECK_LUA_INT( "callBool('xgl_bool_ent', 'RetNum', {})", 0, "CallSENTBool: non-boolean -> 0" );
	CHECK_LUA_INT( "callBool('xgl_bool_ent', 'Missing', {})", 0, "CallSENTBool: missing -> 0" );
	CHECK_LUA_INT( "callBool('xgl_bool_ent', 'RetErr', {})", 0, "CallSENTBool: error -> 0" );
	CHECK_LUA_INT( "select(1, errors())", 1, "CallSENTBool: error reported once" );
}

int main()
{
	xtest::Setup() = SetupSENT;
	return xtest::RunAll( "scripted_ents" );
}
