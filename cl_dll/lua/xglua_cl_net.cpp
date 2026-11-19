/***
*
*	XGLua — Client-side networking (M3 Phase 3B)
*
*	Receives "XGLNet" user messages from the server, handles internal
*	string-table sync (XGLUA_NET_FLAG_STRTBL), reassembles fragments, and
*	dispatches net.Receive inside XGLua_ProtectedCall. Outbound messages
*	fragment and Base64-encode into ServerCmd("xgln ...").
*
****/

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// Netcodec headers are self-contained and must precede cl_util.h, whose
// fabs/index macros clash with MSVC <cmath>.
#include "lua/xglua_netcodec.h"
#include "lua/xglua_netfrag.h"
#include "lua/xglua_nwvar.h"
#include "hud.h"
#include "cl_util.h"
#include "lua/xglua_cl.h"
#include "lua/xglua_safe.h"
extern "C" {
#include "lauxlib.h"
}

#define XGLUA_CL_NET_RECEIVERS "XGLua_CL_NetReceivers"

static std::vector<std::string> g_CLNetStrings;
static XGLuaNetReassembler g_CLReassembler;
static XGLuaNWStore g_CLNWStore;
static uint16_t g_CLNextSeq = 1;

static bool g_CLWriting = false;
static uint16_t g_CLWriteMsgId = 0;
static uint8_t g_CLWriteBuf[XGLUA_NET_MAX_BYTES];
static XGLuaNetWriter g_CLWriter;

static bool g_CLReading = false;
static XGLuaNetReader g_CLReader;

static std::string CLNetNormalize( const char *name )
{
	std::string s;
	if( !name ) return s;
	s.reserve( std::strlen( name ) );
	for( const char *p = name; *p; ++p )
	{
		char c = *p;
		if( c >= 'A' && c <= 'Z' ) c = (char)( c + ( 'a' - 'A' ) );
		s.push_back( c );
	}
	return s;
}

static uint16_t CLNetFindID( const char *name )
{
	std::string norm = CLNetNormalize( name );
	if( norm.empty() ) return 0;
	for( size_t i = 0; i < g_CLNetStrings.size(); ++i )
	{
		if( g_CLNetStrings[i] == norm )
			return (uint16_t)( i + 1 );
	}
	return 0;
}

static void PushCLReceivers( lua_State *L )
{
	lua_pushlightuserdata( L, (void *)XGLUA_CL_NET_RECEIVERS );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if( lua_istable( L, -1 ) ) return;
	lua_pop( L, 1 );
	lua_newtable( L );
	lua_pushvalue( L, -1 );
	lua_pushlightuserdata( L, (void *)XGLUA_CL_NET_RECEIVERS );
	lua_insert( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

static XGLuaNetWriter *RequireCLWriter( lua_State *L )
{
	if( !g_CLWriting )
		luaL_error( L, "net.Write* called without an active net.Start!" );
	return &g_CLWriter;
}

static XGLuaNetReader *RequireCLReader( lua_State *L )
{
	if( !g_CLReading )
		luaL_error( L, "net.Read* called outside a net.Receive callback!" );
	return &g_CLReader;
}

//===========================================================================
// Lua bindings
//===========================================================================

static int clnet_Start( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	uint16_t id = CLNetFindID( name );
	if( id == 0 )
		return luaL_error( L, "net.Start: unknown network string '%s'", name );
	g_CLWriting = true;
	g_CLWriteMsgId = id;
	g_CLWriter.Init( g_CLWriteBuf, sizeof( g_CLWriteBuf ) );
	return 0;
}

static int clnet_WriteBool( lua_State *L )   { RequireCLWriter( L )->WriteBool( lua_toboolean( L, 1 ) != 0 ); return 0; }
static int clnet_WriteInt( lua_State *L )
{
	int32_t v = (int32_t)luaL_checknumber( L, 1 );
	int bits = (int)luaL_checkinteger( L, 2 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 2, "bits must be 1..32" );
	RequireCLWriter( L )->WriteInt( v, bits );
	return 0;
}
static int clnet_WriteUInt( lua_State *L )
{
	uint32_t v = (uint32_t)luaL_checknumber( L, 1 );
	int bits = (int)luaL_checkinteger( L, 2 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 2, "bits must be 1..32" );
	RequireCLWriter( L )->WriteUInt( v, bits );
	return 0;
}
static int clnet_WriteFloat( lua_State *L )  { RequireCLWriter( L )->WriteFloat( (float)luaL_checknumber( L, 1 ) ); return 0; }
static int clnet_WriteDouble( lua_State *L ) { RequireCLWriter( L )->WriteDouble( (double)luaL_checknumber( L, 1 ) ); return 0; }
static int clnet_WriteString( lua_State *L ) { RequireCLWriter( L )->WriteString( luaL_checkstring( L, 1 ) ); return 0; }
static int clnet_WriteData( lua_State *L )
{
	size_t slen = 0;
	const char *s = luaL_checklstring( L, 1, &slen );
	int req = (int)luaL_optinteger( L, 2, (lua_Integer)slen );
	if( req < 0 || (size_t)req > slen ) return luaL_argerror( L, 2, "invalid length" );
	RequireCLWriter( L )->WriteBytes( (const uint8_t *)s, (size_t)req );
	return 0;
}

static int clnet_ReadBool( lua_State *L ) { lua_pushboolean( L, RequireCLReader( L )->ReadBool() ? 1 : 0 ); return 1; }
static int clnet_ReadInt( lua_State *L )
{
	int bits = (int)luaL_checkinteger( L, 1 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 1, "bits must be 1..32" );
	lua_pushnumber( L, (double)RequireCLReader( L )->ReadInt( bits ) );
	return 1;
}
static int clnet_ReadUInt( lua_State *L )
{
	int bits = (int)luaL_checkinteger( L, 1 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 1, "bits must be 1..32" );
	lua_pushnumber( L, (double)RequireCLReader( L )->ReadUInt( bits ) );
	return 1;
}
static int clnet_ReadFloat( lua_State *L )  { lua_pushnumber( L, (double)RequireCLReader( L )->ReadFloat() ); return 1; }
static int clnet_ReadDouble( lua_State *L ) { lua_pushnumber( L, RequireCLReader( L )->ReadDouble() ); return 1; }
static int clnet_ReadString( lua_State *L )
{
	char buf[2048];
	if( !RequireCLReader( L )->ReadString( buf, sizeof( buf ) ) )
		lua_pushliteral( L, "" );
	else
		lua_pushstring( L, buf );
	return 1;
}
static int clnet_ReadData( lua_State *L )
{
	int len = (int)luaL_checkinteger( L, 1 );
	if( len < 0 || len > XGLUA_NET_MAX_BYTES ) return luaL_argerror( L, 1, "invalid length" );
	if( len == 0 ) { lua_pushliteral( L, "" ); return 1; }
	static uint8_t tmp[XGLUA_NET_MAX_BYTES];
	if( !RequireCLReader( L )->ReadBytes( tmp, (size_t)len ) )
		lua_pushliteral( L, "" );
	else
		lua_pushlstring( L, (const char *)tmp, (size_t)len );
	return 1;
}

static int clnet_SendToServer( lua_State *L )
{
	XGLuaNetWriter *w = RequireCLWriter( L );
	g_CLWriting = false;
	if( w->overflow )
		return luaL_error( L, "net.SendToServer: message overflowed buffer" );

	const size_t total = w->BytesWritten();
	// 120 raw bytes -> 160 Base64 chars, keeping full "xgln ..." command < 200 chars.
	const size_t chunkMax = 120;
	size_t fragCount = total == 0 ? 1 : ( ( total + chunkMax - 1 ) / chunkMax );
	if( fragCount > XGLUA_NET_MAX_FRAGS )
		return luaL_error( L, "net.SendToServer: message too large" );

	uint16_t seq = g_CLNextSeq++;
	if( g_CLNextSeq == 0 ) g_CLNextSeq = 1;

	for( size_t i = 0; i < fragCount; ++i )
	{
		size_t off = i * chunkMax;
		size_t n = total > off ? ( total - off ) : 0;
		if( n > chunkMax ) n = chunkMax;

		char b64[200] = { 0 };
		if( n > 0 )
			XGLua_Base64Encode( g_CLWriteBuf + off, n, b64, sizeof( b64 ) );

		char cmd[256];
		snprintf( cmd, sizeof( cmd ), "xgln %u %u %u %u %s\n",
			(unsigned)seq, (unsigned)i, (unsigned)fragCount, (unsigned)g_CLWriteMsgId, b64 );
		ServerCmd( cmd );
	}
	return 0;
}

static int clnw_GetBool( lua_State *L )
{
	const XGLuaNWValue *v = g_CLNWStore.Get( (uint16_t)luaL_checkinteger( L, 1 ), luaL_checkstring( L, 2 ) );
	lua_pushboolean( L, ( v && v->type == XGLUA_NW_BOOL ) ? ( v->b ? 1 : 0 ) : lua_toboolean( L, 3 ) );
	return 1;
}
static int clnw_GetInt( lua_State *L )
{
	const XGLuaNWValue *v = g_CLNWStore.Get( (uint16_t)luaL_checkinteger( L, 1 ), luaL_checkstring( L, 2 ) );
	lua_pushnumber( L, ( v && v->type == XGLUA_NW_INT ) ? (double)v->i : luaL_optnumber( L, 3, 0 ) );
	return 1;
}
static int clnw_GetFloat( lua_State *L )
{
	const XGLuaNWValue *v = g_CLNWStore.Get( (uint16_t)luaL_checkinteger( L, 1 ), luaL_checkstring( L, 2 ) );
	lua_pushnumber( L, ( v && v->type == XGLUA_NW_FLOAT ) ? (double)v->f : luaL_optnumber( L, 3, 0 ) );
	return 1;
}
static int clnw_GetString( lua_State *L )
{
	const XGLuaNWValue *v = g_CLNWStore.Get( (uint16_t)luaL_checkinteger( L, 1 ), luaL_checkstring( L, 2 ) );
	lua_pushstring( L, ( v && v->type == XGLUA_NW_STRING ) ? v->s.c_str() : luaL_optstring( L, 3, "" ) );
	return 1;
}

static int clnet_Receive( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	if( !lua_isnil( L, 2 ) )
		luaL_checktype( L, 2, LUA_TFUNCTION );
	std::string norm = CLNetNormalize( name );
	PushCLReceivers( L );
	lua_pushlstring( L, norm.data(), norm.size() );
	lua_pushvalue( L, 2 );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	return 0;
}

//===========================================================================
// Incoming XGLNet user message handler
//===========================================================================

struct XGLua_CLNetRecvCtx { const char *name; size_t bitLen; };

static int XGLua_CLNetRecvCore( lua_State *L, void *ud )
{
	XGLua_CLNetRecvCtx *rc = (XGLua_CLNetRecvCtx *)ud;
	PushCLReceivers( L );
	lua_pushstring( L, rc->name );
	lua_rawget( L, -2 );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushinteger( L, (lua_Integer)rc->bitLen );
	if( lua_pcall( L, 1, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

static void HandleStringTableSync( const std::vector<uint8_t> &payload )
{
	XGLuaNetReader r;
	r.Init( payload.data(), payload.size() );
	uint32_t count = r.ReadUInt( 16 );
	if( r.underflow || count > 65535 )
		return;
	g_CLNetStrings.clear();
	XGLua_CL_MemVFSClear();
	g_CLNWStore.Reset();
	for( uint32_t i = 0; i < count; ++i )
	{
		uint32_t id = r.ReadUInt( 16 );
		char name[128] = { 0 };
		if( !r.ReadString( name, sizeof( name ) ) || id == 0 )
			break;
		if( id > g_CLNetStrings.size() )
			g_CLNetStrings.resize( id );
		g_CLNetStrings[id - 1] = CLNetNormalize( name );
	}
}

static void HandleCSLuaFile( const std::vector<uint8_t> &payload )
{
	if( CVAR_GET_FLOAT( "xgl_cl_allowdownload" ) == 0.0f )
		return;
	XGLuaNetReader r;
	r.Init( payload.data(), payload.size() );
	char path[256] = { 0 };
	if( !r.ReadString( path, sizeof( path ) ) || !path[0] )
		return;
	if( path[0] == '/' || path[0] == '\\' || std::strstr( path, ".." ) != NULL || std::strchr( path, ':' ) != NULL )
		return;
	uint32_t fileLen = r.ReadUInt( 16 );
	if( r.underflow || fileLen > XGLUA_NET_MAX_BYTES )
		return;
	std::string content;
	content.resize( fileLen );
	if( fileLen > 0 && !r.ReadBytes( (uint8_t *)&content[0], fileLen ) )
		return;
	XGLua_CL_MemVFSStore( path, content.data(), content.size() );
}

static int __MsgFunc_XGLNet( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	if( !pbuf || iSize < XGLUA_NET_HDR_BYTES )
		return 1;

	const uint8_t *raw = (const uint8_t *)pbuf;
	XGLuaNetFragHeader hdr;
	if( !XGLua_NetDecodeHeader( raw, (size_t)iSize, &hdr ) )
		return 1;

	std::vector<uint8_t> full;
	if( !g_CLReassembler.PushFragment( hdr, raw + XGLUA_NET_HDR_BYTES, (size_t)iSize - XGLUA_NET_HDR_BYTES, &full ) )
		return 1;

	if( hdr.flags & XGLUA_NET_FLAG_STRTBL )
	{
		HandleStringTableSync( full );
		return 1;
	}

	if( hdr.flags & XGLUA_NET_FLAG_CSLUA )
	{
		HandleCSLuaFile( full );
		return 1;
	}

	if( hdr.flags & XGLUA_NET_FLAG_NWVAR )
	{
		XGLuaNetReader r;
		r.Init( full.data(), full.size() );
		g_CLNWStore.ApplyPacket( r );
		return 1;
	}

	if( !g_xglua_cl_L || hdr.msgId == 0 || hdr.msgId > g_CLNetStrings.size() )
		return 1;

	const std::string &name = g_CLNetStrings[hdr.msgId - 1];
	if( name.empty() )
		return 1;

	g_CLReader.Init( full.data(), full.size() );
	g_CLReading = true;
	XGLua_CLNetRecvCtx rc = { name.c_str(), full.size() * 8 };
	XGLua_ProtectedCall( g_xglua_cl_L, XGLua_CLNetRecvCore, &rc, XGLua_CL_ReportError, "net.Receive" );
	g_CLReading = false;
	return 1;
}

void XGLua_CL_NetHookMessages( void )
{
	CVAR_CREATE( "xgl_cl_allowdownload", "1", FCVAR_ARCHIVE );
	gEngfuncs.pfnHookUserMsg( "XGLNet", __MsgFunc_XGLNet );
}

void XGLua_CL_NetReset( void )
{
	g_CLWriting = false;
	g_CLReading = false;
	g_CLReassembler.Reset();
	g_CLNetStrings.clear();
}

void XGLua_CL_RegisterNet( lua_State *L )
{
	lua_newtable( L );
	static const luaL_Reg netlib[] =
	{
		{ "Start",        clnet_Start },
		{ "WriteBool",    clnet_WriteBool },
		{ "WriteInt",     clnet_WriteInt },
		{ "WriteUInt",    clnet_WriteUInt },
		{ "WriteFloat",   clnet_WriteFloat },
		{ "WriteDouble",  clnet_WriteDouble },
		{ "WriteString",  clnet_WriteString },
		{ "WriteData",    clnet_WriteData },
		{ "ReadBool",     clnet_ReadBool },
		{ "ReadInt",      clnet_ReadInt },
		{ "ReadUInt",     clnet_ReadUInt },
		{ "ReadFloat",    clnet_ReadFloat },
		{ "ReadDouble",   clnet_ReadDouble },
		{ "ReadString",   clnet_ReadString },
		{ "ReadData",     clnet_ReadData },
		{ "SendToServer", clnet_SendToServer },
		{ "Receive",      clnet_Receive },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = netlib; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "net" );

	lua_newtable( L );
	static const luaL_Reg nwlib[] =
	{
		{ "GetBool",   clnw_GetBool },
		{ "GetInt",    clnw_GetInt },
		{ "GetFloat",  clnw_GetFloat },
		{ "GetString", clnw_GetString },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = nwlib; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "nw" );
}
