// Regression for lua/includes/modules/net.lua (WriteTable/ReadTable) and
// XGLUA_NET_FLAG_CSLUA framing/reassembly.
// Run from tests/xglua with /I<repo>/common.

#include "test_framework.h"
#include "lua/xglua_netcodec.h"
#include "lua/xglua_netfrag.h"

static uint8_t g_buf[XGLUA_NET_MAX_BYTES];
static XGLuaNetWriter g_w;
static XGLuaNetReader g_r;

static int fx_start( lua_State *L )       { (void)L; g_w.Init( g_buf, sizeof( g_buf ) ); return 0; }
static int fx_flip( lua_State *L )        { (void)L; g_r.Init( g_buf, g_w.BytesWritten() ); return 0; }
static int fx_wbool( lua_State *L )       { g_w.WriteBool( lua_toboolean( L, 1 ) != 0 ); return 0; }
static int fx_wuint( lua_State *L )       { g_w.WriteUInt( (uint32_t)luaL_checkinteger( L, 1 ), (int)luaL_checkinteger( L, 2 ) ); return 0; }
static int fx_wdouble( lua_State *L )     { g_w.WriteDouble( luaL_checknumber( L, 1 ) ); return 0; }
static int fx_wstring( lua_State *L )     { g_w.WriteString( luaL_checkstring( L, 1 ) ); return 0; }
static int fx_rbool( lua_State *L )       { lua_pushboolean( L, g_r.ReadBool() ? 1 : 0 ); return 1; }
static int fx_ruint( lua_State *L )       { lua_pushinteger( L, (lua_Integer)g_r.ReadUInt( (int)luaL_checkinteger( L, 1 ) ) ); return 1; }
static int fx_rdouble( lua_State *L )     { lua_pushnumber( L, g_r.ReadDouble() ); return 1; }
static int fx_rstring( lua_State *L )
{
	char s[512] = { 0 };
	g_r.ReadString( s, sizeof( s ) );
	lua_pushstring( L, s );
	return 1;
}

static bool SetupNetLua( lua_State *L )
{
	lua_newtable( L );
	static const luaL_Reg fns[] = {
		{ "WriteBool", fx_wbool }, { "WriteUInt", fx_wuint },
		{ "WriteDouble", fx_wdouble }, { "WriteString", fx_wstring },
		{ "ReadBool", fx_rbool }, { "ReadUInt", fx_ruint },
		{ "ReadDouble", fx_rdouble }, { "ReadString", fx_rstring },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = fns; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "net" );
	lua_pushcfunction( L, fx_start ); lua_setglobal( L, "netResetWrite" );
	lua_pushcfunction( L, fx_flip );  lua_setglobal( L, "netFlipToRead" );

	if( luaL_loadfile( L, "../../lua/includes/modules/net.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: net.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

TEST(NetTable_roundtrip_nested_and_mixed)
{
	LUA( "netResetWrite()\n"
		"net.WriteTable({ a = 1, b = 'hi', c = true, d = false, [1] = 'arr', nested = { x = -3.5, y = { z = 42 } }, skip = function() end })\n"
		"netFlipToRead()\n"
		"Out = net.ReadTable()" );
	CHECK_LUA_INT( "Out.a", 1, "WriteTable: integer value" );
	CHECK_LUA_STR( "Out.b", "hi", "WriteTable: string value" );
	CHECK_LUA_BOOL( "Out.c", true, "WriteTable: true value" );
	CHECK_LUA_BOOL( "Out.d", false, "WriteTable: false value" );
	CHECK_LUA_STR( "Out[1]", "arr", "WriteTable: array key" );
	CHECK_LUA_FLT( "Out.nested.x", -3.5, "WriteTable: nested float" );
	CHECK_LUA_INT( "Out.nested.y.z", 42, "WriteTable: 2-level nested int" );
	CHECK_LUA_NIL( "Out.skip", "WriteTable: function field skipped" );
}

TEST(NetTable_cycle_and_depth_guards)
{
	LUA( "local cyc = {}; cyc.self = cyc; Cyc = cyc" );
	CHECK_LUA_ERROR( "netResetWrite(); net.WriteTable(Cyc)", "cyclic table", "WriteTable: rejects cyclic table" );

	LUA( "local deep = {}; local cur = deep; for i = 1, 10 do cur.n = {}; cur = cur.n end; Deep = deep" );
	CHECK_LUA_ERROR( "netResetWrite(); net.WriteTable(Deep)", "max nesting depth", "WriteTable: rejects depth > 8" );
}

TEST(CSLua_flag_fragment_roundtrip)
{
	const char *path = "autorun/xgl_welcome.lua";
	const char *src = "print('hello from server cslua')";
	uint8_t raw[256];
	XGLuaNetWriter w;
	w.Init( raw, sizeof( raw ) );
	CHECK( w.WriteString( path ), "encode path" );
	CHECK( w.WriteUInt( (uint32_t)std::strlen( src ), 16 ), "encode len" );
	CHECK( w.WriteBytes( (const uint8_t *)src, std::strlen( src ) ), "encode body" );

	XGLuaNetReassembler re;
	re.Reset();
	XGLuaNetFragHeader hdr = { XGLUA_NET_FLAG_CSLUA, 1, 0, 0, 1 };
	std::vector<uint8_t> full;
	CHECK( re.PushFragment( hdr, raw, w.BytesWritten(), &full ), "single-frag reassembly" );

	XGLuaNetReader r;
	r.Init( full.data(), full.size() );
	char outPath[128] = { 0 };
	CHECK( r.ReadString( outPath, sizeof( outPath ) ), "decode path" );
	CHECK_EQ_STR( outPath, path, "path matches" );
	uint32_t len = r.ReadUInt( 16 );
	CHECK_EQ_INT( (int)len, (int)std::strlen( src ), "length matches" );
	char outBody[128] = { 0 };
	CHECK( r.ReadBytes( (uint8_t *)outBody, len ), "decode body" );
	CHECK_EQ_STR( outBody, src, "body matches" );
}

int main()
{
	xtest::Setup() = SetupNetLua;
	return xtest::RunAll( "net_cslua" );
}
