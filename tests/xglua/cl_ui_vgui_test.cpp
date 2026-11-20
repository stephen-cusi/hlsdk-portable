// Regression for lua/includes/modules/draw.lua & vgui.lua (M7-7C).
// Zero engine/VGUI dependencies; links only against LuaJIT.

#define HSPRITE XGLUA_TEST_WIN_HSPRITE
#include "test_framework.h"
#undef HSPRITE

static int g_rectCalls = 0;

static int stub_SetDrawColor( lua_State *L ) { (void)L; return 0; }
static int stub_DrawRect( lua_State *L )     { (void)L; ++g_rectCalls; return 0; }
static int stub_SetFont( lua_State *L )      { (void)L; return 0; }
static int stub_SetTextColor( lua_State *L ) { (void)L; return 0; }
static int stub_SetTextPos( lua_State *L )   { (void)L; return 0; }
static int stub_DrawText( lua_State *L )     { (void)L; return 0; }
static int stub_GetTextSize( lua_State *L )
{
	const char *s = luaL_optstring( L, 1, "" );
	lua_pushinteger( L, (lua_Integer)std::strlen( s ) * 8 );
	lua_pushinteger( L, 14 );
	return 2;
}
static int stub_ScrW( lua_State *L ) { lua_pushinteger( L, 1280 ); return 1; }
static int stub_ScrH( lua_State *L ) { lua_pushinteger( L, 720 ); return 1; }
static int stub_Color( lua_State *L )
{
	lua_Integer r = (lua_Integer)luaL_optnumber( L, 1, 255 );
	lua_Integer g = (lua_Integer)luaL_optnumber( L, 2, 255 );
	lua_Integer b = (lua_Integer)luaL_optnumber( L, 3, 255 );
	lua_Integer a = (lua_Integer)luaL_optnumber( L, 4, 255 );

	lua_createtable( L, 0, 4 );
	lua_pushinteger( L, r ); lua_setfield( L, -2, "r" );
	lua_pushinteger( L, g ); lua_setfield( L, -2, "g" );
	lua_pushinteger( L, b ); lua_setfield( L, -2, "b" );
	lua_pushinteger( L, a ); lua_setfield( L, -2, "a" );
	return 1;
}

static bool SetupUITest( lua_State *L )
{
	g_rectCalls = 0;
	lua_pushboolean( L, 1 ); lua_setglobal( L, "CLIENT" );
	lua_pushinteger( L, 107 ); lua_setglobal( L, "MOUSE_LEFT" );
	lua_pushcfunction( L, stub_ScrW ); lua_setglobal( L, "ScrW" );
	lua_pushcfunction( L, stub_ScrH ); lua_setglobal( L, "ScrH" );
	lua_pushcfunction( L, stub_Color ); lua_setglobal( L, "Color" );

	lua_newtable( L );
	static const luaL_Reg surf[] = {
		{ "SetDrawColor", stub_SetDrawColor }, { "DrawRect", stub_DrawRect },
		{ "SetFont", stub_SetFont }, { "SetTextColor", stub_SetTextColor },
		{ "SetTextPos", stub_SetTextPos }, { "DrawText", stub_DrawText },
		{ "GetTextSize", stub_GetTextSize }, { NULL, NULL }
	};
	for( const luaL_Reg *f = surf; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "surface" );

	if( luaL_loadfile( L, "../../lua/includes/modules/draw.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: draw.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	if( luaL_loadfile( L, "../../lua/includes/modules/vgui.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: vgui.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

TEST(Draw_rounded_box_scanlines)
{
	g_rectCalls = 0;
	LUA( "draw.RoundedBox( 4, 10, 20, 100, 40, Color(255,0,0) )" );
	// 1 center body + 4 top rows + 4 bottom rows = 9 DrawRect calls
	CHECK_EQ_INT( g_rectCalls, 9, "RoundedBox(r=4) emits 9 scanline rects" );
}

TEST(VGUI_hierarchy_coords_and_click_dispatch)
{
	LUA(
		"Clicked = 0\n"
		"HoverEvents = 0\n"
		"local CustomCard = vgui.Register( 'TestCard', {\n"
		"  Init = function(self) self:SetSize(200, 100) end\n"
		"}, 'Panel' )\n"
		"Card = vgui.Create( 'TestCard' )\n"
		"Card:SetPos( 100, 50 )\n"
		"Btn = vgui.Create( 'DButton', Card )\n"
		"Btn:SetPos( 20, 30 )\n"
		"Btn:SetSize( 80, 25 )\n"
		"Btn:SetText( 'Spawn' )\n"
		"Btn.OnCursorEntered = function() HoverEvents = HoverEvents + 1 end\n"
		"Btn.DoClick = function() Clicked = Clicked + 1 end\n"
		"Sx, Sy = Btn:LocalToScreen( 5, 5 )\n"
		"-- Move inside Btn (screen 120..200, 80..105) and click\n"
		"XGLua_VGUI_Step( 140, 90, false )\n"
		"XGLua_VGUI_Step( 140, 90, true )\n"
		"XGLua_VGUI_Step( 140, 90, false )\n"
	);
	CHECK_LUA_INT( "Sx", 125, "LocalToScreen X = 100 + 20 + 5" );
	CHECK_LUA_INT( "Sy", 85, "LocalToScreen Y = 50 + 30 + 5" );
	CHECK_LUA_INT( "HoverEvents", 1, "OnCursorEntered fired once" );
	CHECK_LUA_INT( "Clicked", 1, "DButton DoClick fired on mouse release inside" );

	LUA( "Card:Remove()" );
	CHECK_LUA_BOOL( "Card:IsValid()", false, "Card invalidated after Remove" );
	CHECK_LUA_BOOL( "Btn:IsValid()", false, "Child Btn invalidated recursively" );
}

int main()
{
	xtest::Setup() = SetupUITest;
	return xtest::RunAll( "cl_ui_vgui" );
}
