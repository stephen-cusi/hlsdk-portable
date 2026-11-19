#ifndef XGLUA_NETFRAG_H
#define XGLUA_NETFRAG_H

// XGLNet framing, fragmentation/reassembly, token-bucket rate limiting,
// and Base64 codec for client->server string commands ("xgln").
//
// Wire header (7 bytes, little-endian):
//   [0]    flags     (0x00 = Lua net message, 0x01 = internal string-table sync)
//   [1..2] seq       (uint16_t message sequence)
//   [3..4] msgId     (uint16_t network string ID)
//   [5]    fragIndex (0 .. fragCount - 1)
//   [6]    fragCount (1 .. 255)
// Payload: up to XGLUA_NET_FRAG_PAYLOAD (160) bytes -> total <= 167 bytes,
// safely below GoldSrc's 192-byte user-message ceiling.

#include <cstdint>
#include <cstring>
#include <vector>

#define XGLUA_NET_HDR_BYTES    7
#define XGLUA_NET_FRAG_PAYLOAD 160
#define XGLUA_NET_MAX_FRAGS    255
#define XGLUA_NET_FLAG_STRTBL  0x01

struct XGLuaNetFragHeader
{
	uint8_t  flags;
	uint16_t seq;
	uint16_t msgId;
	uint8_t  fragIndex;
	uint8_t  fragCount;
};

static inline void XGLua_NetEncodeHeader( uint8_t out[XGLUA_NET_HDR_BYTES], const XGLuaNetFragHeader &h )
{
	out[0] = h.flags;
	out[1] = (uint8_t)( h.seq & 0xFF );
	out[2] = (uint8_t)( ( h.seq >> 8 ) & 0xFF );
	out[3] = (uint8_t)( h.msgId & 0xFF );
	out[4] = (uint8_t)( ( h.msgId >> 8 ) & 0xFF );
	out[5] = h.fragIndex;
	out[6] = h.fragCount;
}

static inline bool XGLua_NetDecodeHeader( const uint8_t *in, size_t len, XGLuaNetFragHeader *out )
{
	if( !in || !out || len < XGLUA_NET_HDR_BYTES )
		return false;
	out->flags = in[0];
	out->seq = (uint16_t)( in[1] | ( (uint16_t)in[2] << 8 ) );
	out->msgId = (uint16_t)( in[3] | ( (uint16_t)in[4] << 8 ) );
	out->fragIndex = in[5];
	out->fragCount = in[6];
	if( out->fragCount == 0 || out->fragIndex >= out->fragCount )
		return false;
	return true;
}

// Reassembles one logical message at a time (per peer). Receiving a new seq
// resets any incomplete prior message.
struct XGLuaNetReassembler
{
	bool active;
	uint8_t flags;
	uint16_t seq;
	uint16_t msgId;
	uint8_t fragCount;
	uint8_t receivedCount;
	bool received[XGLUA_NET_MAX_FRAGS];
	size_t fragLens[XGLUA_NET_MAX_FRAGS];
	uint8_t data[XGLUA_NET_MAX_FRAGS][XGLUA_NET_FRAG_PAYLOAD];

	void Reset()
	{
		active = false;
		flags = 0;
		seq = 0;
		msgId = 0;
		fragCount = 0;
		receivedCount = 0;
		std::memset( received, 0, sizeof( received ) );
		std::memset( fragLens, 0, sizeof( fragLens ) );
	}

	// Returns true when all fragments of the message have arrived and writes
	// the contiguous payload into outPayload.
	bool PushFragment( const XGLuaNetFragHeader &h, const uint8_t *payload, size_t payloadLen,
		std::vector<uint8_t> *outPayload )
	{
		if( h.fragCount == 0 || h.fragIndex >= h.fragCount || payloadLen > XGLUA_NET_FRAG_PAYLOAD )
			return false;
		if( !active || seq != h.seq || fragCount != h.fragCount || msgId != h.msgId || flags != h.flags )
		{
			Reset();
			active = true;
			flags = h.flags;
			seq = h.seq;
			msgId = h.msgId;
			fragCount = h.fragCount;
		}
		if( !received[h.fragIndex] )
		{
			received[h.fragIndex] = true;
			fragLens[h.fragIndex] = payloadLen;
			if( payloadLen > 0 && payload )
				std::memcpy( data[h.fragIndex], payload, payloadLen );
			++receivedCount;
		}
		if( receivedCount < fragCount )
			return false;

		if( outPayload )
		{
			size_t total = 0;
			for( uint8_t i = 0; i < fragCount; ++i ) total += fragLens[i];
			outPayload->resize( total );
			size_t off = 0;
			for( uint8_t i = 0; i < fragCount; ++i )
			{
				if( fragLens[i] > 0 )
					std::memcpy( outPayload->data() + off, data[i], fragLens[i] );
				off += fragLens[i];
			}
		}
		Reset();
		return true;
	}
};

// Token bucket for outbound byte budgeting and inbound message rate limiting.
struct XGLuaNetBucket
{
	double tokens;
	double capacity;
	double ratePerSec;
	double lastTime;

	void Init( double rate, double burst, double now )
	{
		ratePerSec = rate > 0.0 ? rate : 1.0;
		capacity = burst > 0.0 ? burst : ratePerSec;
		tokens = capacity;
		lastTime = now;
	}

	void Refill( double now )
	{
		if( now > lastTime )
		{
			double dt = now - lastTime;
			if( dt > 1.0 ) dt = 1.0; // clamp stall spikes
			tokens += dt * ratePerSec;
			if( tokens > capacity ) tokens = capacity;
			lastTime = now;
		}
		else if( now < lastTime )
		{
			// map change or clock reset
			lastTime = now;
		}
	}

	bool TryConsume( double cost, double now )
	{
		Refill( now );
		if( tokens < cost )
			return false;
		tokens -= cost;
		return true;
	}
};

// Compact Base64 encode/decode for client->server "xgln" commands.
static inline size_t XGLua_Base64Encode( const uint8_t *in, size_t len, char *out, size_t outCap )
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t needed = ( ( len + 2 ) / 3 ) * 4;
	if( !out || outCap < needed + 1 )
		return 0;
	size_t o = 0;
	for( size_t i = 0; i < len; i += 3 )
	{
		uint32_t v = (uint32_t)in[i] << 16;
		if( i + 1 < len ) v |= (uint32_t)in[i + 1] << 8;
		if( i + 2 < len ) v |= (uint32_t)in[i + 2];
		out[o++] = tbl[( v >> 18 ) & 63];
		out[o++] = tbl[( v >> 12 ) & 63];
		out[o++] = ( i + 1 < len ) ? tbl[( v >> 6 ) & 63] : '=';
		out[o++] = ( i + 2 < len ) ? tbl[v & 63] : '=';
	}
	out[o] = '\0';
	return o;
}

static inline int XGLua_B64Val( char c )
{
	if( c >= 'A' && c <= 'Z' ) return c - 'A';
	if( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if( c >= '0' && c <= '9' ) return c - '0' + 52;
	if( c == '+' ) return 62;
	if( c == '/' ) return 63;
	return -1;
}

static inline bool XGLua_Base64Decode( const char *in, uint8_t *out, size_t outCap, size_t *outLen )
{
	if( !in || !out || !outLen )
		return false;
	size_t len = std::strlen( in );
	if( len == 0 ) { *outLen = 0; return true; }
	if( ( len & 3 ) != 0 )
		return false;
	size_t o = 0;
	for( size_t i = 0; i < len; i += 4 )
	{
		int a = XGLua_B64Val( in[i] );
		int b = XGLua_B64Val( in[i + 1] );
		int c = ( in[i + 2] == '=' ) ? -2 : XGLua_B64Val( in[i + 2] );
		int d = ( in[i + 3] == '=' ) ? -2 : XGLua_B64Val( in[i + 3] );
		if( a < 0 || b < 0 || c == -1 || d == -1 )
			return false;
		if( c == -2 && d != -2 )
			return false;
		if( ( c == -2 || d == -2 ) && i + 4 != len )
			return false;

		uint32_t v = ( (uint32_t)a << 18 ) | ( (uint32_t)b << 12 ) |
			( c >= 0 ? ( (uint32_t)c << 6 ) : 0 ) | ( d >= 0 ? (uint32_t)d : 0 );
		if( o >= outCap ) return false;
		out[o++] = (uint8_t)( ( v >> 16 ) & 0xFF );
		if( c >= 0 )
		{
			if( o >= outCap ) return false;
			out[o++] = (uint8_t)( ( v >> 8 ) & 0xFF );
		}
		if( d >= 0 )
		{
			if( o >= outCap ) return false;
			out[o++] = (uint8_t)( v & 0xFF );
		}
	}
	*outLen = o;
	return true;
}

#endif // XGLUA_NETFRAG_H
