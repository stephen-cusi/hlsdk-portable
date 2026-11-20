// Regression for common/lua/xglua_nwvar.h and lua/includes/modules/nwvar.lua.
// Build with /I<repo>/common and run from tests/xglua.

#include "test_framework.h"
#include "lua/xglua_nwvar.h"

static bool SetupNWVar( lua_State *L )
{
	if( luaL_loadfile( L, "../../lua/includes/modules/nwvar.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: nwvar.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

TEST(NWStore_delta_dedup_snapshot_and_clear)
{
	XGLuaNWStore sv, cl;
	CHECK( sv.Set( 5, "hp", XGLuaNWValue::MakeInt( 100 ) ), "first Set marks dirty" );
	CHECK_EQ_BOOL( sv.Set( 5, "hp", XGLuaNWValue::MakeInt( 100 ) ), false, "identical Set is deduplicated" );
	CHECK( sv.Set( 5, "label", XGLuaNWValue::MakeString( "Medkit" ) ), "Set string" );
	CHECK( sv.Set( 5, "pos", XGLuaNWValue::MakeVector( 1.5f, -2.0f, 3.25f ) ), "Set vector" );
	CHECK( sv.Set( 5, "active", XGLuaNWValue::MakeBool( true ) ), "Set bool" );

	uint8_t buf[512];
	XGLuaNetWriter w;
	w.Init( buf, sizeof( buf ) );
	CHECK( sv.EncodeDelta( w ), "EncodeDelta succeeds" );

	// Second delta with no changes must be empty
	XGLuaNetWriter w2;
	w2.Init( buf, sizeof( buf ) );
	CHECK_EQ_BOOL( sv.EncodeDelta( w2 ), false, "second EncodeDelta is empty" );

	// Re-encode snapshot for a newly joined client
	XGLuaNetWriter ws;
	ws.Init( buf, sizeof( buf ) );
	CHECK( sv.EncodeSnapshot( ws ), "EncodeSnapshot succeeds" );

	XGLuaNetReader r;
	r.Init( buf, ws.BytesWritten() );
	CHECK( cl.ApplyPacket( r ), "client ApplyPacket succeeds" );

	const XGLuaNWValue *hp = cl.Get( 5, "hp" );
	CHECK( hp && hp->type == XGLUA_NW_INT && hp->i == 100, "client got Int 100" );
	const XGLuaNWValue *lbl = cl.Get( 5, "label" );
	CHECK( lbl && lbl->type == XGLUA_NW_STRING && lbl->s == "Medkit", "client got String" );
	const XGLuaNWValue *pos = cl.Get( 5, "pos" );
	CHECK( pos && pos->type == XGLUA_NW_VECTOR && pos->v[0] == 1.5f && pos->v[2] == 3.25f, "client got Vector" );
	const XGLuaNWValue *act = cl.Get( 5, "active" );
	CHECK( act && act->type == XGLUA_NW_BOOL && act->b == true, "client got Bool" );

	// Clear entity and sync delta
	sv.ClearEntity( 5, true );
	XGLuaNetWriter wc;
	wc.Init( buf, sizeof( buf ) );
	CHECK( sv.EncodeDelta( wc ), "EncodeDelta carries OP_CLEAR_ENT" );
	XGLuaNetReader rc;
	rc.Init( buf, wc.BytesWritten() );
	CHECK( cl.ApplyPacket( rc ), "client applies clear" );
	CHECK( cl.Get( 5, "hp" ) == NULL, "client entity 5 cleared" );
}

TEST(NWVar_lua_codegen_and_guards)
{
	LUA( "local store = {}\n"
		"Ent = {\n"
		"  SetNWInt = function(self, k, v) store[k] = v end,\n"
		"  GetNWInt = function(self, k, d) return store[k] ~= nil and store[k] or d end,\n"
		"  SetNWString = function(self, k, v) store[k] = v end,\n"
		"  GetNWString = function(self, k, d) return store[k] ~= nil and store[k] or d end\n"
		"}\n"
		"XGLua_InstallNetworkVar( Ent, 'Int', 0, 'Charge' )\n"
		"XGLua_InstallNetworkVar( Ent, 'String', 1, 'Title' )" );
	CHECK_LUA_INT( "Ent:GetCharge()", 0, "NetworkVar Int default 0" );
	LUA( "Ent:SetCharge( 25 ); Ent:SetTitle( 'Big Medkit' )" );
	CHECK_LUA_INT( "Ent:GetCharge()", 25, "NetworkVar Int roundtrip" );
	CHECK_LUA_STR( "Ent:GetTitle()", "Big Medkit", "NetworkVar String roundtrip" );

	CHECK_LUA_ERROR( "XGLua_InstallNetworkVar( Ent, 'BadType', 0, 'X' )", "unsupported type", "rejects bad type" );
	CHECK_LUA_ERROR( "XGLua_InstallNetworkVar( Ent, 'Int', 32, 'X' )", "slot must be an integer 0..31", "rejects slot > 31" );
}

int main()
{
	xtest::Setup() = SetupNWVar;
	return xtest::RunAll( "nwvar" );
}
