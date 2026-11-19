#ifndef XGLUA_NETCODEC_H
#define XGLUA_NETCODEC_H

// Bit-level network serializer/deserializer shared by server, client, and
// host tests. Zero engine/Lua dependencies; no exceptions, no RTTI.

#include <cstdint>
#include <cstring>
#include <cmath>

#define XGLUA_NET_MAX_BYTES 40960 // 40 KB hard cap per logical net message

struct XGLuaNetWriter
{
	uint8_t *buf;
	size_t capBytes;
	size_t bitPos;
	bool overflow;

	void Init( uint8_t *storage, size_t capacityBytes )
	{
		buf = storage;
		capBytes = capacityBytes;
		bitPos = 0;
		overflow = false;
		if( buf && capBytes > 0 )
			std::memset( buf, 0, capBytes );
	}

	size_t BytesWritten() const { return ( bitPos + 7 ) >> 3; }
	size_t BitsWritten() const { return bitPos; }

	bool WriteBits( uint64_t value, int numBits )
	{
		if( overflow || !buf || numBits <= 0 || numBits > 64 )
		{
			if( numBits <= 0 || numBits > 64 ) overflow = true;
			return false;
		}
		if( bitPos + (size_t)numBits > capBytes * 8 )
		{
			overflow = true;
			return false;
		}
		for( int i = 0; i < numBits; ++i )
		{
			if( ( value >> i ) & 1ULL )
				buf[bitPos >> 3] |= (uint8_t)( 1u << ( bitPos & 7 ) );
			++bitPos;
		}
		return true;
	}

	bool WriteBool( bool b ) { return WriteBits( b ? 1ULL : 0ULL, 1 ); }

	bool WriteUInt( uint32_t v, int bits )
	{
		if( bits < 1 || bits > 32 ) { overflow = true; return false; }
		return WriteBits( (uint64_t)v, bits );
	}

	bool WriteInt( int32_t v, int bits )
	{
		if( bits < 1 || bits > 32 ) { overflow = true; return false; }
		return WriteBits( (uint64_t)(uint32_t)v, bits );
	}

	bool WriteFloat( float f )
	{
		uint32_t u = 0;
		std::memcpy( &u, &f, sizeof( u ) );
		return WriteBits( (uint64_t)u, 32 );
	}

	bool WriteDouble( double d )
	{
		uint64_t u = 0;
		std::memcpy( &u, &d, sizeof( u ) );
		return WriteBits( u, 64 );
	}

	bool WriteBytes( const uint8_t *data, size_t len )
	{
		if( !data && len > 0 ) { overflow = true; return false; }
		for( size_t i = 0; i < len; ++i )
		{
			if( !WriteBits( data[i], 8 ) )
				return false;
		}
		return true;
	}

	bool WriteString( const char *str )
	{
		if( !str ) str = "";
		while( *str )
		{
			if( !WriteBits( (uint8_t)*str++, 8 ) )
				return false;
		}
		return WriteBits( 0, 8 );
	}

	bool WriteVec3( float x, float y, float z )
	{
		return WriteFloat( x ) && WriteFloat( y ) && WriteFloat( z );
	}
};

struct XGLuaNetReader
{
	const uint8_t *buf;
	size_t totalBits;
	size_t bitPos;
	bool underflow;

	void Init( const uint8_t *data, size_t byteLen )
	{
		buf = data;
		totalBits = byteLen * 8;
		bitPos = 0;
		underflow = false;
	}

	size_t BitsLeft() const { return bitPos <= totalBits ? ( totalBits - bitPos ) : 0; }

	uint64_t ReadBits( int numBits )
	{
		if( underflow || !buf || numBits <= 0 || numBits > 64 || bitPos + (size_t)numBits > totalBits )
		{
			underflow = true;
			return 0;
		}
		uint64_t out = 0;
		for( int i = 0; i < numBits; ++i )
		{
			if( ( buf[bitPos >> 3] >> ( bitPos & 7 ) ) & 1u )
				out |= ( 1ULL << i );
			++bitPos;
		}
		return out;
	}

	bool ReadBool() { return ReadBits( 1 ) != 0; }

	uint32_t ReadUInt( int bits )
	{
		if( bits < 1 || bits > 32 ) { underflow = true; return 0; }
		return (uint32_t)ReadBits( bits );
	}

	int32_t ReadInt( int bits )
	{
		if( bits < 1 || bits > 32 ) { underflow = true; return 0; }
		uint32_t u = (uint32_t)ReadBits( bits );
		if( bits < 32 )
		{
			uint32_t signBit = 1u << ( bits - 1 );
			if( u & signBit )
				u |= ~( ( 1u << bits ) - 1u );
		}
		return (int32_t)u;
	}

	float ReadFloat()
	{
		uint32_t u = (uint32_t)ReadBits( 32 );
		float f = 0.0f;
		std::memcpy( &f, &u, sizeof( f ) );
		return f;
	}

	double ReadDouble()
	{
		uint64_t u = ReadBits( 64 );
		double d = 0.0;
		std::memcpy( &d, &u, sizeof( d ) );
		return d;
	}

	bool ReadBytes( uint8_t *out, size_t len )
	{
		if( !out && len > 0 ) { underflow = true; return false; }
		for( size_t i = 0; i < len; ++i )
		{
			out[i] = (uint8_t)ReadBits( 8 );
			if( underflow ) return false;
		}
		return true;
	}

	// Reads a null-terminated string into out (always NUL-terminated).
	// Returns false on underflow or if the string exceeds maxLen - 1.
	bool ReadString( char *out, size_t maxLen )
	{
		if( !out || maxLen == 0 ) { underflow = true; return false; }
		size_t n = 0;
		for( ;; )
		{
			uint8_t c = (uint8_t)ReadBits( 8 );
			if( underflow )
			{
				out[0] = '\0';
				return false;
			}
			if( c == 0 )
			{
				out[n] = '\0';
				return true;
			}
			if( n + 1 < maxLen )
				out[n++] = (char)c;
		}
	}

	bool ReadVec3( float *x, float *y, float *z )
	{
		float a = ReadFloat();
		float b = ReadFloat();
		float c = ReadFloat();
		if( underflow ) return false;
		if( x ) *x = a;
		if( y ) *y = b;
		if( z ) *z = c;
		return true;
	}
};

#endif // XGLUA_NETCODEC_H
