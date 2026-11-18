/***
*
*	XGLua — Vector / Angle userdata types (GMod-style metatables)
*
*	Plain userdata + C metatable. (FFI cdata is the future optimization
*	path; this version works everywhere including sandboxed builds.)
*
****/

#include <cmath>
#include <cstdio>
#include <cstring>

#include "xglua.h"
extern "C" {
#include "lauxlib.h"
}

struct XGLuaVec { float x, y, z; };

#define XGLUA_VECTOR_META "XGLua_Vector"
#define XGLUA_ANGLE_META  "XGLua_Angle"

static XGLuaVec *XGLua_NewVec( lua_State *L, float x, float y, float z )
{
	XGLuaVec *v = (XGLuaVec *)lua_newuserdata( L, sizeof( XGLuaVec ) );
	v->x = x; v->y = y; v->z = z;
	luaL_getmetatable( L, XGLUA_VECTOR_META );
	lua_setmetatable( L, -2 );
	return v;
}

static XGLuaVec *XGLua_CheckVec( lua_State *L, int idx )
{
	return (XGLuaVec *)luaL_checkudata( L, idx, XGLUA_VECTOR_META );
}

static XGLuaVec *XGLua_NewAngle( lua_State *L, float p, float y, float r )
{
	XGLuaVec *v = (XGLuaVec *)lua_newuserdata( L, sizeof( XGLuaVec ) );
	v->x = p; v->y = y; v->z = r;
	luaL_getmetatable( L, XGLUA_ANGLE_META );
	lua_setmetatable( L, -2 );
	return v;
}

static XGLuaVec *XGLua_CheckAngleData( lua_State *L, int idx )
{
	return (XGLuaVec *)luaL_checkudata( L, idx, XGLUA_ANGLE_META );
}

void XGLua_PushVector( lua_State *L, const Vector &v )
{
	XGLua_NewVec( L, v.x, v.y, v.z );
}

void XGLua_PushAngle( lua_State *L, const Vector &v )
{
	XGLua_NewAngle( L, v.x, v.y, v.z );
}

Vector XGLua_CheckVector( lua_State *L, int idx )
{
	XGLuaVec *v = XGLua_CheckVec( L, idx );
	return Vector( v->x, v->y, v->z );
}

Vector XGLua_CheckAngle( lua_State *L, int idx )
{
	XGLuaVec *v = XGLua_CheckAngleData( L, idx );
	return Vector( v->x, v->y, v->z );
}

//===========================================================================
// Vector
//===========================================================================

static int Vec_Construct( lua_State *L )
{
	float x = (float)luaL_optnumber( L, 1, 0 );
	float y = (float)luaL_optnumber( L, 2, 0 );
	float z = (float)luaL_optnumber( L, 3, 0 );
	XGLua_NewVec( L, x, y, z );
	return 1;
}

static int Vec_Length( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	lua_pushnumber( L, sqrtf( v->x * v->x + v->y * v->y + v->z * v->z ) );
	return 1;
}

static int Vec_Length2D( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	lua_pushnumber( L, sqrtf( v->x * v->x + v->y * v->y ) );
	return 1;
}

static int Vec_LengthSqr( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	lua_pushnumber( L, v->x * v->x + v->y * v->y + v->z * v->z );
	return 1;
}

static int Vec_Normalize( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	float len = sqrtf( v->x * v->x + v->y * v->y + v->z * v->z );
	if( len > 0.0001f )
	{
		v->x /= len; v->y /= len; v->z /= len;
	}
	lua_pushnumber( L, len );	// GMod returns the length
	return 1;
}

static int Vec_GetNormalized( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	float len = sqrtf( v->x * v->x + v->y * v->y + v->z * v->z );
	if( len > 0.0001f )
		XGLua_NewVec( L, v->x / len, v->y / len, v->z / len );
	else
		XGLua_NewVec( L, 0, 0, 0 );
	return 1;
}

static int Vec_Distance( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
	lua_pushnumber( L, sqrtf( dx * dx + dy * dy + dz * dz ) );
	return 1;
}

static int Vec_Dot( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	lua_pushnumber( L, a->x * b->x + a->y * b->y + a->z * b->z );
	return 1;
}

static int Vec_Cross( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	XGLua_NewVec( L, a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x );
	return 1;
}

// Vector:Angle() — pitch/yaw pointing along this vector (GMod semantics)
static int Vec_Angle( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	float yaw = 0, pitch = 0;
	if( fabsf( v->x ) > 0.0001f || fabsf( v->y ) > 0.0001f )
		yaw = atan2f( v->y, v->x ) * ( 180.0f / 3.14159265f );
	if( fabsf( v->x ) > 0.0001f || fabsf( v->y ) > 0.0001f || fabsf( v->z ) > 0.0001f )
	{
		float fwd = sqrtf( v->x * v->x + v->y * v->y );
		pitch = atan2f( -v->z, fwd ) * ( 180.0f / 3.14159265f );
	}
	XGLua_NewAngle( L, pitch, yaw, 0 );
	return 1;
}

static int Vec_IsZero( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	lua_pushboolean( L, v->x == 0 && v->y == 0 && v->z == 0 );
	return 1;
}

static int Vec_Index( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	const char *k = luaL_checkstring( L, 2 );
	if( k[1] == '\0' )
	{
		switch( k[0] )
		{
		case 'x': case 'X': lua_pushnumber( L, v->x ); return 1;
		case 'y': case 'Y': lua_pushnumber( L, v->y ); return 1;
		case 'z': case 'Z': lua_pushnumber( L, v->z ); return 1;
		}
	}
	luaL_getmetatable( L, XGLUA_VECTOR_META );
	lua_pushvalue( L, 2 );
	lua_rawget( L, -2 );
	lua_remove( L, -2 );
	return 1;
}

static int Vec_NewIndex( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	const char *k = luaL_checkstring( L, 2 );
	float val = (float)luaL_checknumber( L, 3 );
	if( k[1] == '\0' )
	{
		switch( k[0] )
		{
		case 'x': case 'X': v->x = val; return 0;
		case 'y': case 'Y': v->y = val; return 0;
		case 'z': case 'Z': v->z = val; return 0;
		}
	}
	return luaL_error( L, "invalid Vector key: %s", k );
}

static int Vec_Add( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	XGLua_NewVec( L, a->x + b->x, a->y + b->y, a->z + b->z );
	return 1;
}

static int Vec_Sub( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	XGLua_NewVec( L, a->x - b->x, a->y - b->y, a->z - b->z );
	return 1;
}

static int Vec_Mul( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	float s = (float)luaL_checknumber( L, 2 );
	XGLua_NewVec( L, a->x * s, a->y * s, a->z * s );
	return 1;
}

static int Vec_Unm( lua_State *L )
{
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLua_NewVec( L, -a->x, -a->y, -a->z );
	return 1;
}

static int Vec_Eq( lua_State *L )
{
	if( !lua_isuserdata( L, 1 ) || !lua_isuserdata( L, 2 ) )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	XGLuaVec *a = XGLua_CheckVec( L, 1 );
	XGLuaVec *b = XGLua_CheckVec( L, 2 );
	lua_pushboolean( L, a->x == b->x && a->y == b->y && a->z == b->z );
	return 1;
}

static int Vec_ToString( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckVec( L, 1 );
	char buf[64];
	snprintf( buf, sizeof( buf ), "%.2f %.2f %.2f", v->x, v->y, v->z );
	lua_pushstring( L, buf );
	return 1;
}

//===========================================================================
// Angle
//===========================================================================

static int Ang_Construct( lua_State *L )
{
	float p = (float)luaL_optnumber( L, 1, 0 );
	float y = (float)luaL_optnumber( L, 2, 0 );
	float r = (float)luaL_optnumber( L, 3, 0 );
	XGLua_NewAngle( L, p, y, r );
	return 1;
}

static int Ang_Index( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckAngleData( L, 1 );
	const char *k = luaL_checkstring( L, 2 );
	if( !strcmp( k, "pitch" ) ) { lua_pushnumber( L, v->x ); return 1; }
	if( !strcmp( k, "yaw" ) )   { lua_pushnumber( L, v->y ); return 1; }
	if( !strcmp( k, "roll" ) )  { lua_pushnumber( L, v->z ); return 1; }
	if( k[1] == '\0' )
	{
		switch( k[0] )
		{
		case 'p': lua_pushnumber( L, v->x ); return 1;
		case 'y': lua_pushnumber( L, v->y ); return 1;
		case 'r': lua_pushnumber( L, v->z ); return 1;
		}
	}
	luaL_getmetatable( L, XGLUA_ANGLE_META );
	lua_pushvalue( L, 2 );
	lua_rawget( L, -2 );
	lua_remove( L, -2 );
	return 1;
}

static int Ang_NewIndex( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckAngleData( L, 1 );
	const char *k = luaL_checkstring( L, 2 );
	float val = (float)luaL_checknumber( L, 3 );
	if( !strcmp( k, "pitch" ) ) { v->x = val; return 0; }
	if( !strcmp( k, "yaw" ) )   { v->y = val; return 0; }
	if( !strcmp( k, "roll" ) )  { v->z = val; return 0; }
	if( k[1] == '\0' )
	{
		switch( k[0] )
		{
		case 'p': v->x = val; return 0;
		case 'y': v->y = val; return 0;
		case 'r': v->z = val; return 0;
		}
	}
	return luaL_error( L, "invalid Angle key: %s", k );
}

static int Ang_ToString( lua_State *L )
{
	XGLuaVec *v = XGLua_CheckAngleData( L, 1 );
	char buf[64];
	snprintf( buf, sizeof( buf ), "%.2f %.2f %.2f", v->x, v->y, v->z );
	lua_pushstring( L, buf );
	return 1;
}

static int Ang_Zero( lua_State *L )
{
	XGLua_CheckAngleData( L, 1 )->x = XGLua_CheckAngleData( L, 1 )->y = XGLua_CheckAngleData( L, 1 )->z = 0;
	return 0;
}

//===========================================================================
// registration
//===========================================================================

void XGLua_RegisterTypes( lua_State *L )
{
	// Vector
	luaL_newmetatable( L, XGLUA_VECTOR_META );
	const luaL_Reg vecmeta[] =
	{
		{ "Length", Vec_Length },
		{ "Length2D", Vec_Length2D },
		{ "LengthSqr", Vec_LengthSqr },
		{ "Normalize", Vec_Normalize },
		{ "GetNormalized", Vec_GetNormalized },
		{ "Distance", Vec_Distance },
		{ "Dot", Vec_Dot },
		{ "Cross", Vec_Cross },
		{ "Angle", Vec_Angle },
		{ "IsZero", Vec_IsZero },
		{ "__index", Vec_Index },
		{ "__newindex", Vec_NewIndex },
		{ "__add", Vec_Add },
		{ "__sub", Vec_Sub },
		{ "__mul", Vec_Mul },
		{ "__unm", Vec_Unm },
		{ "__eq", Vec_Eq },
		{ "__tostring", Vec_ToString },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = vecmeta; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_pop( L, 1 );

	// Angle
	luaL_newmetatable( L, XGLUA_ANGLE_META );
	const luaL_Reg angmeta[] =
	{
		{ "Zero", Ang_Zero },
		{ "__index", Ang_Index },
		{ "__newindex", Ang_NewIndex },
		{ "__tostring", Ang_ToString },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = angmeta; f->name; f++ )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_pop( L, 1 );

	// constructors
	lua_pushcfunction( L, Vec_Construct );
	lua_setglobal( L, "Vector" );
	lua_pushcfunction( L, Ang_Construct );
	lua_setglobal( L, "Angle" );
}
