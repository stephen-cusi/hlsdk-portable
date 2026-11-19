/***
*
*	XGLua — Server-side networking (M3 Phase 3A)
*
*	Single variable-length user message "XGLNet" for server->client delivery
*	with 160-byte payload fragmentation and per-client token-bucket pacing.
*	Client->server messages arrive via the "xgln" ClientCommand with Base64
*	payloads, per-client rate limiting, and protected net.Receive dispatch.
*
****/

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <deque>

#include "xglua.h"
#include "xglua_safe.h"
#include "lua/xglua_netcodec.h"
#include "lua/xglua_netfrag.h"
#include "player.h"
extern "C" {
#include "lauxlib.h"
}

#define XGLUA_NET_RECEIVERS "XGLua_NetReceivers"
#define XGLUA_MAX_CLIENTS   32

static int gmsgXGLNet = 0;

static cvar_t xgl_net_bps         = { "xgl_net_bps", "8192" };
static cvar_t xgl_net_queue_limit = { "xgl_net_queue_limit", "256" };
static cvar_t xgl_net_rate_limit  = { "xgl_net_rate_limit", "60" };

//===========================================================================
// Network string table (1-based IDs; case-insensitive like GMod)
//===========================================================================

static std::vector<std::string> g_NetStrings;

#define XGLUA_CSLUA_MAX_FILE_BYTES 36864 // 36 KB per file (< XGLUA_NET_MAX_BYTES)
static std::vector<std::string> g_CSLuaFiles;

static bool NetNormalizeCSLuaPath( const char *raw, std::string &out )
{
	if( !raw || !raw[0] ) return false;
	std::string s = raw;
	for( size_t i = 0; i < s.size(); ++i )
		if( s[i] == '\\' ) s[i] = '/';
	// Strip leading "@lua/" or "lua/" if derived from lua_Debug::source
	if( s.compare( 0, 5, "@lua/" ) == 0 ) s.erase( 0, 5 );
	else if( s.compare( 0, 4, "lua/" ) == 0 ) s.erase( 0, 4 );
	else if( !s.empty() && s[0] == '@' ) s.erase( 0, 1 );

	if( s.empty() || s[0] == '/' || s.find( ".." ) != std::string::npos || s.find( ':' ) != std::string::npos )
		return false;
	out = s;
	return true;
}

static void NetAddCSLuaPath( const std::string &rel )
{
	for( size_t i = 0; i < g_CSLuaFiles.size(); ++i )
		if( g_CSLuaFiles[i] == rel ) return;
	g_CSLuaFiles.push_back( rel );
}

static int global_AddCSLuaFile( lua_State *L )
{
	const char *arg = luaL_optstring( L, 1, NULL );
	std::string rel;
	if( arg && arg[0] )
	{
		if( !NetNormalizeCSLuaPath( arg, rel ) )
			return luaL_argerror( L, 1, "invalid AddCSLuaFile path" );
	}
	else
	{
		lua_Debug ar;
		if( !lua_getstack( L, 1, &ar ) || !lua_getinfo( L, "S", &ar ) || !ar.source )
			return 0;
		if( !NetNormalizeCSLuaPath( ar.source, rel ) )
			return 0;
	}
	NetAddCSLuaPath( rel );
	return 0;
}


static std::string NetNormalizeName( const char *name )
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

static uint16_t NetFindStringID( const char *name )
{
	std::string norm = NetNormalizeName( name );
	if( norm.empty() ) return 0;
	for( size_t i = 0; i < g_NetStrings.size(); ++i )
	{
		if( g_NetStrings[i] == norm )
			return (uint16_t)( i + 1 );
	}
	return 0;
}

static uint16_t NetAddString( const char *name )
{
	uint16_t existing = NetFindStringID( name );
	if( existing != 0 )
		return existing;
	std::string norm = NetNormalizeName( name );
	if( norm.empty() || g_NetStrings.size() >= 65535 )
		return 0;
	g_NetStrings.push_back( norm );
	return (uint16_t)g_NetStrings.size();
}

//===========================================================================
// Per-client queues, reassemblers, and rate limiters
//===========================================================================

struct XGLuaQueuedFrag
{
	uint8_t bytes[XGLUA_NET_HDR_BYTES + XGLUA_NET_FRAG_PAYLOAD];
	size_t len;
};

struct XGLuaNetClient
{
	std::deque<XGLuaQueuedFrag> outQueue;
	XGLuaNetBucket outBucket;
	XGLuaNetBucket inRate;
	XGLuaNetReassembler reassembler;
	uint16_t nextSeq;
	bool connected;

	void Reset( double now )
	{
		outQueue.clear();
		double bps = xgl_net_bps.value > 256.0f ? (double)xgl_net_bps.value : 8192.0;
		double rps = xgl_net_rate_limit.value > 1.0f ? (double)xgl_net_rate_limit.value : 60.0;
		outBucket.Init( bps, bps * 0.25 > 1024.0 ? bps * 0.25 : 1024.0, now );
		inRate.Init( rps, rps, now );
		reassembler.Reset();
		nextSeq = 1;
		connected = false;
	}
};

static XGLuaNetClient g_NetClients[XGLUA_MAX_CLIENTS + 1];

static bool NetEnqueueMessage( int clientIdx, uint8_t flags, uint16_t msgId, const uint8_t *payload, size_t payloadLen )
{
	if( clientIdx < 1 || clientIdx > XGLUA_MAX_CLIENTS )
		return false;
	XGLuaNetClient &cl = g_NetClients[clientIdx];
	if( !cl.connected )
		return false;

	size_t fragCount = payloadLen == 0 ? 1 : ( ( payloadLen + XGLUA_NET_FRAG_PAYLOAD - 1 ) / XGLUA_NET_FRAG_PAYLOAD );
	if( fragCount > XGLUA_NET_MAX_FRAGS )
	{
		ALERT( at_console, "[XGLua] net: message too large (%u bytes) for client %d\n", (unsigned)payloadLen, clientIdx );
		return false;
	}

	size_t limit = xgl_net_queue_limit.value > 16.0f ? (size_t)xgl_net_queue_limit.value : 256;
	if( cl.outQueue.size() + fragCount > limit )
	{
		ALERT( at_console, "[XGLua] net: outbound queue overflow for client %d, dropping message\n", clientIdx );
		return false;
	}

	uint16_t seq = cl.nextSeq++;
	if( cl.nextSeq == 0 ) cl.nextSeq = 1;

	for( size_t i = 0; i < fragCount; ++i )
	{
		size_t off = i * XGLUA_NET_FRAG_PAYLOAD;
		size_t chunk = payloadLen > off ? ( payloadLen - off ) : 0;
		if( chunk > XGLUA_NET_FRAG_PAYLOAD ) chunk = XGLUA_NET_FRAG_PAYLOAD;

		XGLuaNetFragHeader hdr = { flags, seq, msgId, (uint8_t)i, (uint8_t)fragCount };
		XGLuaQueuedFrag qf;
		XGLua_NetEncodeHeader( qf.bytes, hdr );
		if( chunk > 0 )
			std::memcpy( qf.bytes + XGLUA_NET_HDR_BYTES, payload + off, chunk );
		qf.len = XGLUA_NET_HDR_BYTES + chunk;
		cl.outQueue.push_back( qf );
	}
	return true;
}

static void NetSyncStringTableToClient( int clientIdx )
{
	if( g_NetStrings.empty() )
		return;
	static uint8_t scratch[XGLUA_NET_MAX_BYTES];
	XGLuaNetWriter w;
	w.Init( scratch, sizeof( scratch ) );
	w.WriteUInt( (uint32_t)g_NetStrings.size(), 16 );
	for( size_t i = 0; i < g_NetStrings.size(); ++i )
	{
		w.WriteUInt( (uint32_t)( i + 1 ), 16 );
		w.WriteString( g_NetStrings[i].c_str() );
	}
	if( !w.overflow )
		NetEnqueueMessage( clientIdx, XGLUA_NET_FLAG_STRTBL, 0, scratch, w.BytesWritten() );
}

static void NetSyncCSLuaFilesToClient( int clientIdx )
{
	if( g_CSLuaFiles.empty() || !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile )
		return;

	static uint8_t scratch[XGLUA_NET_MAX_BYTES];
	for( size_t i = 0; i < g_CSLuaFiles.size(); ++i )
	{
		char full[512];
		snprintf( full, sizeof( full ), "lua/%s", g_CSLuaFiles[i].c_str() );
		int fileLen = 0;
		byte *data = LOAD_FILE_FOR_ME( full, &fileLen );
		if( !data )
		{
			ALERT( at_console, "[XGLua] AddCSLuaFile: missing '%s'\n", full );
			continue;
		}
		if( fileLen >= 0 && fileLen <= XGLUA_CSLUA_MAX_FILE_BYTES )
		{
			XGLuaNetWriter w;
			w.Init( scratch, sizeof( scratch ) );
			w.WriteString( g_CSLuaFiles[i].c_str() );
			w.WriteUInt( (uint32_t)fileLen, 16 );
			w.WriteBytes( (const uint8_t *)data, (size_t)fileLen );
			if( !w.overflow )
				NetEnqueueMessage( clientIdx, XGLUA_NET_FLAG_CSLUA, 0, scratch, w.BytesWritten() );
		}
		else
		{
			ALERT( at_console, "[XGLua] AddCSLuaFile: '%s' exceeds %d bytes, skipped\n",
				full, XGLUA_CSLUA_MAX_FILE_BYTES );
		}
		FREE_FILE( data );
	}
}

//===========================================================================
// Active Lua write/read buffers
//===========================================================================

static bool g_NetWriting = false;
static uint16_t g_NetWriteMsgId = 0;
static uint8_t g_NetWriteBuf[XGLUA_NET_MAX_BYTES];
static XGLuaNetWriter g_NetWriter;

static bool g_NetReading = false;
static XGLuaNetReader g_NetReader;

static XGLuaNetWriter *RequireWriter( lua_State *L )
{
	if( !g_NetWriting )
		luaL_error( L, "net.Write* called without an active net.Start!" );
	return &g_NetWriter;
}

static XGLuaNetReader *RequireReader( lua_State *L )
{
	if( !g_NetReading )
		luaL_error( L, "net.Read* called outside a net.Receive callback!" );
	return &g_NetReader;
}

//===========================================================================
// Lua bindings: util.AddNetworkString / net.*
//===========================================================================

static int util_AddNetworkString( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	uint16_t id = NetAddString( name );
	if( id == 0 )
		return luaL_error( L, "util.AddNetworkString: invalid name or table full" );
	lua_pushinteger( L, id );
	return 1;
}

static int util_NetworkStringToID( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	lua_pushinteger( L, NetFindStringID( name ) );
	return 1;
}

static int util_NetworkIDToString( lua_State *L )
{
	int id = (int)luaL_checkinteger( L, 1 );
	if( id < 1 || (size_t)id > g_NetStrings.size() )
	{
		lua_pushnil( L );
		return 1;
	}
	lua_pushstring( L, g_NetStrings[(size_t)id - 1].c_str() );
	return 1;
}

static int net_Start( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	uint16_t id = NetFindStringID( name );
	if( id == 0 )
		return luaL_error( L, "net.Start: unregistered network string '%s'", name );
	g_NetWriting = true;
	g_NetWriteMsgId = id;
	g_NetWriter.Init( g_NetWriteBuf, sizeof( g_NetWriteBuf ) );
	return 0;
}

static int net_WriteBool( lua_State *L )
{
	RequireWriter( L )->WriteBool( lua_toboolean( L, 1 ) != 0 );
	return 0;
}

static int net_WriteInt( lua_State *L )
{
	int32_t v = (int32_t)luaL_checknumber( L, 1 );
	int bits = (int)luaL_checkinteger( L, 2 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 2, "bits must be 1..32" );
	RequireWriter( L )->WriteInt( v, bits );
	return 0;
}

static int net_WriteUInt( lua_State *L )
{
	uint32_t v = (uint32_t)luaL_checknumber( L, 1 );
	int bits = (int)luaL_checkinteger( L, 2 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 2, "bits must be 1..32" );
	RequireWriter( L )->WriteUInt( v, bits );
	return 0;
}

static int net_WriteFloat( lua_State *L )
{
	RequireWriter( L )->WriteFloat( (float)luaL_checknumber( L, 1 ) );
	return 0;
}

static int net_WriteDouble( lua_State *L )
{
	RequireWriter( L )->WriteDouble( (double)luaL_checknumber( L, 1 ) );
	return 0;
}

static int net_WriteString( lua_State *L )
{
	RequireWriter( L )->WriteString( luaL_checkstring( L, 1 ) );
	return 0;
}

static int net_WriteVector( lua_State *L )
{
	Vector v = XGLua_CheckVector( L, 1 );
	RequireWriter( L )->WriteVec3( v.x, v.y, v.z );
	return 0;
}

static int net_WriteAngle( lua_State *L )
{
	Vector a = XGLua_CheckAngle( L, 1 );
	RequireWriter( L )->WriteVec3( a.x, a.y, a.z );
	return 0;
}

static int net_WriteEntity( lua_State *L )
{
	XGLuaNetWriter *w = RequireWriter( L );
	if( lua_isnil( L, 1 ) )
	{
		w->WriteUInt( 0, 16 );
		return 0;
	}
	edict_t *pent = XGLua_TryEntity( L, 1 );
	int idx = pent ? ENTINDEX( pent ) : 0;
	if( idx < 0 || idx > 65535 ) idx = 0;
	w->WriteUInt( (uint32_t)idx, 16 );
	return 0;
}

static int net_WriteData( lua_State *L )
{
	size_t slen = 0;
	const char *s = luaL_checklstring( L, 1, &slen );
	int req = (int)luaL_optinteger( L, 2, (lua_Integer)slen );
	if( req < 0 || (size_t)req > slen )
		return luaL_argerror( L, 2, "invalid data length" );
	RequireWriter( L )->WriteBytes( (const uint8_t *)s, (size_t)req );
	return 0;
}

static int net_ReadBool( lua_State *L )
{
	lua_pushboolean( L, RequireReader( L )->ReadBool() ? 1 : 0 );
	return 1;
}

static int net_ReadInt( lua_State *L )
{
	int bits = (int)luaL_checkinteger( L, 1 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 1, "bits must be 1..32" );
	lua_pushnumber( L, (double)RequireReader( L )->ReadInt( bits ) );
	return 1;
}

static int net_ReadUInt( lua_State *L )
{
	int bits = (int)luaL_checkinteger( L, 1 );
	if( bits < 1 || bits > 32 ) return luaL_argerror( L, 1, "bits must be 1..32" );
	lua_pushnumber( L, (double)RequireReader( L )->ReadUInt( bits ) );
	return 1;
}

static int net_ReadFloat( lua_State *L )
{
	lua_pushnumber( L, (double)RequireReader( L )->ReadFloat() );
	return 1;
}

static int net_ReadDouble( lua_State *L )
{
	lua_pushnumber( L, RequireReader( L )->ReadDouble() );
	return 1;
}

static int net_ReadString( lua_State *L )
{
	char buf[2048];
	if( !RequireReader( L )->ReadString( buf, sizeof( buf ) ) )
	{
		lua_pushliteral( L, "" );
		return 1;
	}
	lua_pushstring( L, buf );
	return 1;
}

static int net_ReadVector( lua_State *L )
{
	float x = 0, y = 0, z = 0;
	RequireReader( L )->ReadVec3( &x, &y, &z );
	XGLua_PushVector( L, Vector( x, y, z ) );
	return 1;
}

static int net_ReadAngle( lua_State *L )
{
	float p = 0, y = 0, r = 0;
	RequireReader( L )->ReadVec3( &p, &y, &r );
	XGLua_PushAngle( L, Vector( p, y, r ) );
	return 1;
}

static int net_ReadEntity( lua_State *L )
{
	uint32_t idx = RequireReader( L )->ReadUInt( 16 );
	if( idx == 0 || !gpGlobals || (int)idx >= gpGlobals->maxEntities )
	{
		lua_pushnil( L );
		return 1;
	}
	edict_t *pent = INDEXENT( (int)idx );
	if( FNullEnt( pent ) || pent->free )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, pent );
	return 1;
}

static int net_ReadData( lua_State *L )
{
	int len = (int)luaL_checkinteger( L, 1 );
	if( len < 0 || len > XGLUA_NET_MAX_BYTES )
		return luaL_argerror( L, 1, "invalid length" );
	if( len == 0 )
	{
		lua_pushliteral( L, "" );
		return 1;
	}
	static uint8_t tmp[XGLUA_NET_MAX_BYTES];
	if( !RequireReader( L )->ReadBytes( tmp, (size_t)len ) )
	{
		lua_pushliteral( L, "" );
		return 1;
	}
	lua_pushlstring( L, (const char *)tmp, (size_t)len );
	return 1;
}

static int net_Send( lua_State *L )
{
	XGLuaNetWriter *w = RequireWriter( L );
	g_NetWriting = false;
	if( w->overflow )
		return luaL_error( L, "net.Send: message overflowed buffer" );

	const size_t nbytes = w->BytesWritten();
	if( lua_istable( L, 1 ) )
	{
		size_t n = lua_objlen( L, 1 );
		for( size_t i = 1; i <= n; ++i )
		{
			lua_rawgeti( L, 1, (int)i );
			edict_t *pent = XGLua_TryEntity( L, -1 );
			lua_pop( L, 1 );
			if( pent )
				NetEnqueueMessage( ENTINDEX( pent ), 0, g_NetWriteMsgId, g_NetWriteBuf, nbytes );
		}
	}
	else
	{
		edict_t *pent = XGLua_CheckEntity( L, 1 );
		NetEnqueueMessage( ENTINDEX( pent ), 0, g_NetWriteMsgId, g_NetWriteBuf, nbytes );
	}
	return 0;
}

static int net_Broadcast( lua_State *L )
{
	XGLuaNetWriter *w = RequireWriter( L );
	g_NetWriting = false;
	if( w->overflow )
		return luaL_error( L, "net.Broadcast: message overflowed buffer" );

	const size_t nbytes = w->BytesWritten();
	int maxCl = gpGlobals ? gpGlobals->maxClients : 0;
	if( maxCl > XGLUA_MAX_CLIENTS ) maxCl = XGLUA_MAX_CLIENTS;
	for( int i = 1; i <= maxCl; ++i )
	{
		if( g_NetClients[i].connected )
			NetEnqueueMessage( i, 0, g_NetWriteMsgId, g_NetWriteBuf, nbytes );
	}
	return 0;
}

static int net_SendOmit( lua_State *L )
{
	XGLuaNetWriter *w = RequireWriter( L );
	g_NetWriting = false;
	if( w->overflow )
		return luaL_error( L, "net.SendOmit: message overflowed buffer" );

	edict_t *omit = XGLua_TryEntity( L, 1 );
	int omitIdx = omit ? ENTINDEX( omit ) : -1;
	const size_t nbytes = w->BytesWritten();
	int maxCl = gpGlobals ? gpGlobals->maxClients : 0;
	if( maxCl > XGLUA_MAX_CLIENTS ) maxCl = XGLUA_MAX_CLIENTS;
	for( int i = 1; i <= maxCl; ++i )
	{
		if( i != omitIdx && g_NetClients[i].connected )
			NetEnqueueMessage( i, 0, g_NetWriteMsgId, g_NetWriteBuf, nbytes );
	}
	return 0;
}

static void PushReceiverTable( lua_State *L )
{
	lua_pushlightuserdata( L, (void *)XGLUA_NET_RECEIVERS );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if( lua_istable( L, -1 ) ) return;
	lua_pop( L, 1 );
	lua_newtable( L );
	lua_pushvalue( L, -1 );
	lua_pushlightuserdata( L, (void *)XGLUA_NET_RECEIVERS );
	lua_insert( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

static int net_Receive( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	if( !lua_isnil( L, 2 ) )
		luaL_checktype( L, 2, LUA_TFUNCTION );
	std::string norm = NetNormalizeName( name );
	PushReceiverTable( L );
	lua_pushlstring( L, norm.data(), norm.size() );
	lua_pushvalue( L, 2 );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	return 0;
}

//===========================================================================
// Engine lifecycle & dispatch
//===========================================================================

void XGLua_NetInit( void )
{
	CVAR_REGISTER( &xgl_net_bps );
	CVAR_REGISTER( &xgl_net_queue_limit );
	CVAR_REGISTER( &xgl_net_rate_limit );
}

void XGLua_NetLinkUserMessages( void )
{
	if( !gmsgXGLNet )
		gmsgXGLNet = REG_USER_MSG( "XGLNet", -1 );
}

void XGLua_RegisterNetLibs( lua_State *L )
{
	// Attach AddNetworkString / NetworkStringToID / NetworkIDToString to util
	lua_getglobal( L, "util" );
	if( lua_istable( L, -1 ) )
	{
		lua_pushcfunction( L, util_AddNetworkString );
		lua_setfield( L, -2, "AddNetworkString" );
		lua_pushcfunction( L, util_NetworkStringToID );
		lua_setfield( L, -2, "NetworkStringToID" );
		lua_pushcfunction( L, util_NetworkIDToString );
		lua_setfield( L, -2, "NetworkIDToString" );
	}
	lua_pop( L, 1 );

	// net global table
	lua_newtable( L );
	static const luaL_Reg netlib[] =
	{
		{ "Start",       net_Start },
		{ "WriteBool",   net_WriteBool },
		{ "WriteInt",    net_WriteInt },
		{ "WriteUInt",   net_WriteUInt },
		{ "WriteFloat",  net_WriteFloat },
		{ "WriteDouble", net_WriteDouble },
		{ "WriteString", net_WriteString },
		{ "WriteVector", net_WriteVector },
		{ "WriteAngle",  net_WriteAngle },
		{ "WriteEntity", net_WriteEntity },
		{ "WriteData",   net_WriteData },
		{ "ReadBool",    net_ReadBool },
		{ "ReadInt",     net_ReadInt },
		{ "ReadUInt",    net_ReadUInt },
		{ "ReadFloat",   net_ReadFloat },
		{ "ReadDouble",  net_ReadDouble },
		{ "ReadString",  net_ReadString },
		{ "ReadVector",  net_ReadVector },
		{ "ReadAngle",   net_ReadAngle },
		{ "ReadEntity",  net_ReadEntity },
		{ "ReadData",    net_ReadData },
		{ "Send",        net_Send },
		{ "Broadcast",   net_Broadcast },
		{ "SendOmit",    net_SendOmit },
		{ "Receive",     net_Receive },
		{ NULL, NULL }
	};
	for( const luaL_Reg *f = netlib; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, -2, f->name );
	}
	lua_setglobal( L, "net" );

	lua_pushcfunction( L, global_AddCSLuaFile );
	lua_setglobal( L, "AddCSLuaFile" );
}

void XGLua_NetFrame( lua_State *L )
{
	(void)L;
	if( !gmsgXGLNet || !gpGlobals )
		return;

	const double now = (double)gpGlobals->time;
	int maxCl = gpGlobals->maxClients;
	if( maxCl > XGLUA_MAX_CLIENTS ) maxCl = XGLUA_MAX_CLIENTS;

	for( int i = 1; i <= maxCl; ++i )
	{
		XGLuaNetClient &cl = g_NetClients[i];
		if( !cl.connected || cl.outQueue.empty() )
			continue;

		edict_t *pent = INDEXENT( i );
		if( FNullEnt( pent ) || pent->free )
		{
			cl.Reset( now );
			continue;
		}

		// Drain fragments while token bucket allows.
		while( !cl.outQueue.empty() )
		{
			const XGLuaQueuedFrag &qf = cl.outQueue.front();
			if( !cl.outBucket.TryConsume( (double)qf.len, now ) )
				break;

			MESSAGE_BEGIN( MSG_ONE, gmsgXGLNet, NULL, &pent->v );
			for( size_t b = 0; b < qf.len; ++b )
				WRITE_BYTE( qf.bytes[b] );
			MESSAGE_END();

			cl.outQueue.pop_front();
		}
	}
}

void XGLua_NetClientPutInServer( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->edict() )
		return;
	int idx = ENTINDEX( pPlayer->edict() );
	if( idx < 1 || idx > XGLUA_MAX_CLIENTS )
		return;
	double now = gpGlobals ? (double)gpGlobals->time : 0.0;
	g_NetClients[idx].Reset( now );
	g_NetClients[idx].connected = true;
	NetSyncStringTableToClient( idx );
	NetSyncCSLuaFilesToClient( idx );
}

void XGLua_NetClientDisconnect( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->edict() )
		return;
	int idx = ENTINDEX( pPlayer->edict() );
	if( idx < 1 || idx > XGLUA_MAX_CLIENTS )
		return;
	double now = gpGlobals ? (double)gpGlobals->time : 0.0;
	g_NetClients[idx].Reset( now );
}

struct XGLua_NetRecvCtx
{
	const char *msgName;
	size_t bitLen;
	edict_t *sender;
};

static int XGLua_NetRecvCore( lua_State *L, void *ud )
{
	XGLua_NetRecvCtx *rc = (XGLua_NetRecvCtx *)ud;
	PushReceiverTable( L );
	lua_pushstring( L, rc->msgName );
	lua_rawget( L, -2 );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	lua_pushinteger( L, (lua_Integer)rc->bitLen );
	XGLua_PushEntity( L, rc->sender );
	if( lua_pcall( L, 2, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

// Handles client->server command: xgln <seq> <fragIndex> <fragCount> <msgId> <base64>
BOOL XGLua_NetClientCommand( edict_t *pEntity )
{
	if( !g_xglua_L || !pEntity || FNullEnt( pEntity ) )
		return TRUE;

	int idx = ENTINDEX( pEntity );
	if( idx < 1 || idx > XGLUA_MAX_CLIENTS || !g_NetClients[idx].connected )
		return TRUE;

	double now = gpGlobals ? (double)gpGlobals->time : 0.0;
	if( !g_NetClients[idx].inRate.TryConsume( 1.0, now ) )
		return TRUE; // rate-limited silently

	if( CMD_ARGC() < 6 )
		return TRUE;

	int seq       = std::atoi( CMD_ARGV( 1 ) );
	int fragIndex = std::atoi( CMD_ARGV( 2 ) );
	int fragCount = std::atoi( CMD_ARGV( 3 ) );
	int msgId     = std::atoi( CMD_ARGV( 4 ) );
	const char *b64 = CMD_ARGV( 5 );

	if( seq < 0 || seq > 65535 || fragIndex < 0 || fragCount <= 0 ||
		fragCount > XGLUA_NET_MAX_FRAGS || fragIndex >= fragCount ||
		msgId < 1 || (size_t)msgId > g_NetStrings.size() )
		return TRUE;

	uint8_t decoded[XGLUA_NET_FRAG_PAYLOAD];
	size_t decLen = 0;
	if( !XGLua_Base64Decode( b64, decoded, sizeof( decoded ), &decLen ) )
		return TRUE;

	XGLuaNetFragHeader hdr = { 0, (uint16_t)seq, (uint16_t)msgId, (uint8_t)fragIndex, (uint8_t)fragCount };
	std::vector<uint8_t> full;
	if( !g_NetClients[idx].reassembler.PushFragment( hdr, decoded, decLen, &full ) )
		return TRUE;

	g_NetReader.Init( full.data(), full.size() );
	g_NetReading = true;
	XGLua_NetRecvCtx rc = { g_NetStrings[(size_t)msgId - 1].c_str(), full.size() * 8, pEntity };
	XGLua_ProtectedCall( g_xglua_L, XGLua_NetRecvCore, &rc, XGLua_ReportError, "net.Receive" );
	g_NetReading = false;
	return TRUE;
}

void XGLua_NetShutdown( void )
{
	g_NetWriting = false;
	g_NetReading = false;
	g_NetStrings.clear();
	g_CSLuaFiles.clear();
	for( int i = 0; i <= XGLUA_MAX_CLIENTS; ++i )
		g_NetClients[i].Reset( 0.0 );
}
