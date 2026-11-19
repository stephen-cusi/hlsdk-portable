/***
*
*	XGLua — Client-side 2D surface drawing library (M3 Phase 3B)
*
****/

#include "lua/xglua_cl.h" // must precede cl_util.h (macro hygiene)
#include "hud.h"
#include "cl_util.h"
extern "C" {
#include "lauxlib.h"
}

static int g_DrawR = 255, g_DrawG = 255, g_DrawB = 255, g_DrawA = 255;
static int g_TextR = 255, g_TextG = 255, g_TextB = 255, g_TextA = 255;
static int g_TextX = 0, g_TextY = 0;

static int ClampByte( int v ) { return v < 0 ? 0 : ( v > 255 ? 255 : v ); }

static void ReadColorArgs( lua_State *L, int &r, int &g, int &b, int &a )
{
	if( lua_istable( L, 1 ) )
	{
		lua_getfield( L, 1, "r" ); r = ClampByte( (int)luaL_optnumber( L, -1, 255 ) ); lua_pop( L, 1 );
		lua_getfield( L, 1, "g" ); g = ClampByte( (int)luaL_optnumber( L, -1, 255 ) ); lua_pop( L, 1 );
		lua_getfield( L, 1, "b" ); b = ClampByte( (int)luaL_optnumber( L, -1, 255 ) ); lua_pop( L, 1 );
		lua_getfield( L, 1, "a" ); a = ClampByte( (int)luaL_optnumber( L, -1, 255 ) ); lua_pop( L, 1 );
	}
	else
	{
		r = ClampByte( (int)luaL_checknumber( L, 1 ) );
		g = ClampByte( (int)luaL_checknumber( L, 2 ) );
		b = ClampByte( (int)luaL_checknumber( L, 3 ) );
		a = ClampByte( (int)luaL_optnumber( L, 4, 255 ) );
	}
}

static int surface_SetDrawColor( lua_State *L )
{
	ReadColorArgs( L, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	return 0;
}

static int surface_DrawRect( lua_State *L )
{
	int x = (int)luaL_checknumber( L, 1 );
	int y = (int)luaL_checknumber( L, 2 );
	int w = (int)luaL_checknumber( L, 3 );
	int h = (int)luaL_checknumber( L, 4 );
	if( w <= 0 || h <= 0 ) return 0;
	if( gEngfuncs.pfnFillRGBABlend )
		gEngfuncs.pfnFillRGBABlend( x, y, w, h, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	else
		FillRGBA( x, y, w, h, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	return 0;
}

static int surface_DrawOutlinedRect( lua_State *L )
{
	int x = (int)luaL_checknumber( L, 1 );
	int y = (int)luaL_checknumber( L, 2 );
	int w = (int)luaL_checknumber( L, 3 );
	int h = (int)luaL_checknumber( L, 4 );
	int t = (int)luaL_optnumber( L, 5, 1 );
	if( w <= 0 || h <= 0 || t <= 0 ) return 0;
	FillRGBA( x, y, w, t, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	FillRGBA( x, y + h - t, w, t, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	FillRGBA( x, y + t, t, h - 2 * t, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	FillRGBA( x + w - t, y + t, t, h - 2 * t, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
	return 0;
}

static int surface_SetFont( lua_State *L )
{
	(void)luaL_optstring( L, 1, "Default" ); // v1 uses engine console font
	return 0;
}

static int surface_SetTextColor( lua_State *L )
{
	ReadColorArgs( L, g_TextR, g_TextG, g_TextB, g_TextA );
	return 0;
}

static int surface_SetTextPos( lua_State *L )
{
	g_TextX = (int)luaL_checknumber( L, 1 );
	g_TextY = (int)luaL_checknumber( L, 2 );
	return 0;
}

static int surface_DrawText( lua_State *L )
{
	const char *text = luaL_checkstring( L, 1 );
	DrawSetTextColor( g_TextR / 255.0f, g_TextG / 255.0f, g_TextB / 255.0f );
	DrawConsoleString( g_TextX, g_TextY, text );
	return 0;
}

static int surface_GetTextSize( lua_State *L )
{
	const char *text = luaL_checkstring( L, 1 );
	int w = 0, h = 0;
	GetConsoleStringSize( text, &w, &h );
	lua_pushinteger( L, w );
	lua_pushinteger( L, h );
	return 2;
}

void XGLua_CL_RegisterSurface( lua_State *L )
{
	lua_newtable( L );
	static const luaL_Reg surflib[] =
	{
		{ "SetDrawColor",     surface_SetDrawColor },
		{ "DrawRect",         surface_DrawRect },
		{ "DrawOutlinedRect", surface_DrawOutlinedRect },
		{ "SetFont",          surface_SetFont },
		{ "SetTextColor",     surface_SetTextColor },
		{ "SetTextPos",       surface_SetTextPos },
		{ "DrawText",         surface_DrawText },
		{ "GetTextSize",      surface_GetTextSize },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = surflib; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "surface" );
}
