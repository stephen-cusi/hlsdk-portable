/***
*
*	XGLua — Client-side 2D surface, font, texture & cursor/input layer (M7-7C)
*
****/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hud.h"
#include "cl_util.h"
#include "keydefs.h"
#include "lua/xglua_cl.h"
extern "C" {
#include "lauxlib.h"
}

// Satisfies both this TU and cl_dll/hud_redraw.cpp's extern declaration
// (Xash3D FWGS's input_goldsource.cpp no longer defines this global).
int g_iVisibleMouse = 0;

extern "C" void IN_ActivateMouse( void );
extern "C" void IN_DeactivateMouse( void );

//===========================================================================
// Internal state: colors, fonts, textures, cursor
//===========================================================================

struct XGLuaCLFont
{
	std::string name;
	std::string family;
	int size;
	int weight;
	bool antialias;
	bool shadow;
};

struct XGLuaCLTexture
{
	std::string path;
	HSPRITE handle;
	int width;
	int height;
};

static int g_DrawR = 255, g_DrawG = 255, g_DrawB = 255, g_DrawA = 255;
static int g_TextR = 255, g_TextG = 255, g_TextB = 255, g_TextA = 255;
static int g_TextX = 0, g_TextY = 0;

static std::vector<XGLuaCLFont> g_CLFonts;
static int g_ActiveFontIdx = -1;

static std::vector<XGLuaCLTexture> g_CLTextures;
static int g_ActiveTextureId = 0; // 1-based index into g_CLTextures

static bool g_ScreenClickerEnabled = false;

static int ClampByte( int v ) { return v < 0 ? 0 : ( v > 255 ? 255 : v ); }

static std::string NormalizeKey( const char *s )
{
	std::string out;
	if( !s ) return out;
	for( const char *p = s; *p; ++p )
	{
		char c = *p;
		if( c >= 'A' && c <= 'Z' ) c = (char)( c + ( 'a' - 'A' ) );
		if( c == '\\' ) c = '/';
		out.push_back( c );
	}
	return out;
}

static void EnsureDefaultFont( void )
{
	if( !g_CLFonts.empty() ) return;
	XGLuaCLFont def = { "default", "Default", 14, 500, true, false };
	g_CLFonts.push_back( def );
	g_ActiveFontIdx = 0;
}

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

void XGLua_CL_SurfaceReset( void )
{
	g_CLFonts.clear();
	g_ActiveFontIdx = -1;
	g_CLTextures.clear();
	g_ActiveTextureId = 0;
	if( g_ScreenClickerEnabled )
	{
		g_ScreenClickerEnabled = false;
		g_iVisibleMouse = 0;
		IN_ActivateMouse();
	}
}

//===========================================================================
// surface.*
//===========================================================================

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

static int surface_DrawLine( lua_State *L )
{
	int x1 = (int)luaL_checknumber( L, 1 );
	int y1 = (int)luaL_checknumber( L, 2 );
	int x2 = (int)luaL_checknumber( L, 3 );
	int y2 = (int)luaL_checknumber( L, 4 );
	int dx = x2 - x1;
	int dy = y2 - y1;
	int steps = Q_max( abs( dx ), abs( dy ) );
	if( steps == 0 )
	{
		FillRGBA( x1, y1, 1, 1, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
		return 0;
	}
	float xInc = (float)dx / (float)steps;
	float yInc = (float)dy / (float)steps;
	float fx = (float)x1, fy = (float)y1;
	for( int i = 0; i <= steps; ++i )
	{
		FillRGBA( (int)( fx + 0.5f ), (int)( fy + 0.5f ), 1, 1, g_DrawR, g_DrawG, g_DrawB, g_DrawA );
		fx += xInc;
		fy += yInc;
	}
	return 0;
}

static int surface_CreateFont( lua_State *L )
{
	const char *rawName = luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TTABLE );
	EnsureDefaultFont();

	std::string key = NormalizeKey( rawName );
	if( key.empty() )
		return luaL_argerror( L, 1, "font name cannot be empty" );

	XGLuaCLFont f;
	f.name = key;
	lua_getfield( L, 2, "font" );
	f.family = luaL_optstring( L, -1, "Default" );
	lua_pop( L, 1 );

	lua_getfield( L, 2, "size" );
	f.size = (int)luaL_optnumber( L, -1, 14 );
	if( f.size < 6 ) f.size = 6;
	if( f.size > 128 ) f.size = 128;
	lua_pop( L, 1 );

	lua_getfield( L, 2, "weight" );
	f.weight = (int)luaL_optnumber( L, -1, 500 );
	lua_pop( L, 1 );

	lua_getfield( L, 2, "antialias" );
	f.antialias = lua_isnil( L, -1 ) ? true : ( lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 1 );

	lua_getfield( L, 2, "shadow" );
	f.shadow = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );

	for( size_t i = 0; i < g_CLFonts.size(); ++i )
	{
		if( g_CLFonts[i].name == key )
		{
			g_CLFonts[i] = f;
			return 0;
		}
	}
	g_CLFonts.push_back( f );
	return 0;
}

static int surface_SetFont( lua_State *L )
{
	const char *rawName = luaL_optstring( L, 1, "Default" );
	EnsureDefaultFont();
	std::string key = NormalizeKey( rawName );
	for( size_t i = 0; i < g_CLFonts.size(); ++i )
	{
		if( g_CLFonts[i].name == key )
		{
			g_ActiveFontIdx = (int)i;
			return 0;
		}
	}
	g_ActiveFontIdx = 0;
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
	EnsureDefaultFont();
	const XGLuaCLFont *f = ( g_ActiveFontIdx >= 0 && (size_t)g_ActiveFontIdx < g_CLFonts.size() )
		? &g_CLFonts[(size_t)g_ActiveFontIdx] : NULL;

	if( f && f->shadow )
	{
		DrawSetTextColor( 0.0f, 0.0f, 0.0f );
		DrawConsoleString( g_TextX + 1, g_TextY + 1, text );
	}
	DrawSetTextColor( g_TextR / 255.0f, g_TextG / 255.0f, g_TextB / 255.0f );
	DrawConsoleString( g_TextX, g_TextY, text );
	return 0;
}

static int surface_GetTextSize( lua_State *L )
{
	const char *text = luaL_checkstring( L, 1 );
	int w = 0, h = 0;
	GetConsoleStringSize( text, &w, &h );
	EnsureDefaultFont();
	if( g_ActiveFontIdx >= 0 && (size_t)g_ActiveFontIdx < g_CLFonts.size() )
	{
		int targetH = g_CLFonts[(size_t)g_ActiveFontIdx].size;
		if( h > 0 && targetH > h )
			h = targetH;
	}
	lua_pushinteger( L, w );
	lua_pushinteger( L, h );
	return 2;
}

static int surface_GetTextureID( lua_State *L )
{
	const char *raw = luaL_checkstring( L, 1 );
	std::string key = NormalizeKey( raw );
	if( key.empty() )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}
	// Normalize to "sprites/<name>.spr" when caller passes short path
	if( key.compare( 0, 8, "sprites/" ) != 0 )
		key = "sprites/" + key;
	if( key.size() < 4 || key.compare( key.size() - 4, 4, ".spr" ) != 0 )
		key += ".spr";

	for( size_t i = 0; i < g_CLTextures.size(); ++i )
	{
		if( g_CLTextures[i].path == key )
		{
			lua_pushinteger( L, (lua_Integer)( i + 1 ) );
			return 1;
		}
	}

	HSPRITE hspr = SPR_Load( key.c_str() );
	if( !hspr )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	XGLuaCLTexture tex;
	tex.path = key;
	tex.handle = hspr;
	tex.width = SPR_Width( hspr, 0 );
	tex.height = SPR_Height( hspr, 0 );
	g_CLTextures.push_back( tex );
	lua_pushinteger( L, (lua_Integer)g_CLTextures.size() );
	return 1;
}

static int surface_SetTexture( lua_State *L )
{
	g_ActiveTextureId = (int)luaL_checkinteger( L, 1 );
	return 0;
}

static int surface_GetTextureSize( lua_State *L )
{
	int id = (int)luaL_checkinteger( L, 1 );
	if( id < 1 || (size_t)id > g_CLTextures.size() )
	{
		lua_pushinteger( L, 0 );
		lua_pushinteger( L, 0 );
		return 2;
	}
	const XGLuaCLTexture &tex = g_CLTextures[(size_t)id - 1];
	lua_pushinteger( L, tex.width );
	lua_pushinteger( L, tex.height );
	return 2;
}

static int surface_DrawTexturedRect( lua_State *L )
{
	int x = (int)luaL_checknumber( L, 1 );
	int y = (int)luaL_checknumber( L, 2 );
	int w = (int)luaL_optnumber( L, 3, 0 );
	int h = (int)luaL_optnumber( L, 4, 0 );
	if( g_ActiveTextureId < 1 || (size_t)g_ActiveTextureId > g_CLTextures.size() )
		return 0;

	const XGLuaCLTexture &tex = g_CLTextures[(size_t)g_ActiveTextureId - 1];
	if( !tex.handle )
		return 0;

	wrect_t rc;
	rc.left = 0;
	rc.top = 0;
	rc.right = ( w > 0 ) ? Q_min( w, tex.width ) : tex.width;
	rc.bottom = ( h > 0 ) ? Q_min( h, tex.height ) : tex.height;

	SPR_Set( tex.handle, g_DrawR, g_DrawG, g_DrawB );
	SPR_DrawHoles( 0, x, y, &rc );
	return 0;
}

void XGLua_CL_RegisterSurface( lua_State *L )
{
	EnsureDefaultFont();
	lua_newtable( L );
	static const luaL_Reg surflib[] =
	{
		{ "SetDrawColor",     surface_SetDrawColor },
		{ "DrawRect",         surface_DrawRect },
		{ "DrawOutlinedRect", surface_DrawOutlinedRect },
		{ "DrawLine",         surface_DrawLine },
		{ "CreateFont",       surface_CreateFont },
		{ "SetFont",          surface_SetFont },
		{ "SetTextColor",     surface_SetTextColor },
		{ "SetTextPos",       surface_SetTextPos },
		{ "DrawText",         surface_DrawText },
		{ "GetTextSize",      surface_GetTextSize },
		{ "GetTextureID",     surface_GetTextureID },
		{ "SetTexture",       surface_SetTexture },
		{ "GetTextureSize",   surface_GetTextureSize },
		{ "DrawTexturedRect", surface_DrawTexturedRect },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = surflib; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "surface" );
}

//===========================================================================
// gui.* & input.* (cursor unlock, position & mouse button queries)
//===========================================================================

static int gui_EnableScreenClicker( lua_State *L )
{
	bool enable = lua_toboolean( L, 1 ) != 0;
	if( enable == g_ScreenClickerEnabled )
		return 0;
	g_ScreenClickerEnabled = enable;
	g_iVisibleMouse = enable ? 1 : 0;
	if( enable )
		IN_DeactivateMouse();
	else
		IN_ActivateMouse();
	return 0;
}

static int gui_IsScreenClickerEnabled( lua_State *L )
{
	lua_pushboolean( L, g_ScreenClickerEnabled ? 1 : 0 );
	return 1;
}

static int input_GetCursorPos( lua_State *L )
{
	int mx = ScreenWidth / 2;
	int my = ScreenHeight / 2;
	if( gEngfuncs.GetMousePosition )
		gEngfuncs.GetMousePosition( &mx, &my );
	lua_pushinteger( L, mx );
	lua_pushinteger( L, my );
	return 2;
}

static int input_IsMouseDown( lua_State *L )
{
	int code = (int)luaL_checkinteger( L, 1 );
	// GMod MOUSE_LEFT = 107, MOUSE_RIGHT = 108, MOUSE_MIDDLE = 109 (also accept 1/2/3)
	bool down = false;
	if( code == 107 || code == 1 )
		down = ( gHUD.m_iKeyBits & IN_ATTACK ) != 0;
	else if( code == 108 || code == 2 )
		down = ( gHUD.m_iKeyBits & IN_ATTACK2 ) != 0;
	lua_pushboolean( L, down ? 1 : 0 );
	return 1;
}

void XGLua_CL_RegisterInputAndGUI( lua_State *L )
{
	// Mouse button constants matching GMod
	lua_pushinteger( L, 107 ); lua_setglobal( L, "MOUSE_LEFT" );
	lua_pushinteger( L, 108 ); lua_setglobal( L, "MOUSE_RIGHT" );
	lua_pushinteger( L, 109 ); lua_setglobal( L, "MOUSE_MIDDLE" );

	lua_newtable( L );
	lua_pushcfunction( L, gui_EnableScreenClicker );
	lua_setfield( L, -2, "EnableScreenClicker" );
	lua_pushcfunction( L, gui_IsScreenClickerEnabled );
	lua_setfield( L, -2, "IsScreenClickerEnabled" );
	lua_setglobal( L, "gui" );

	lua_newtable( L );
	lua_pushcfunction( L, input_GetCursorPos );
	lua_setfield( L, -2, "GetCursorPos" );
	lua_pushcfunction( L, input_IsMouseDown );
	lua_setfield( L, -2, "IsMouseDown" );
	lua_setglobal( L, "input" );
}
