// Regression for common/lua/xglua_netcodec.h and common/lua/xglua_netfrag.h:
// bitstream round-trip, signed/unsigned bounds, overflow/underflow guards,
// fragmentation/reassembly (including out-of-order and seq reset), token
// bucket pacing, and Base64 codec.
// Add /I<repo>/common to the compiler include path when building.

#include "test_framework.h"
#include "lua/xglua_netcodec.h"
#include "lua/xglua_netfrag.h"

TEST(NetCodec_roundtrip_all_types)
{
	uint8_t buf[256];
	XGLuaNetWriter w;
	w.Init( buf, sizeof( buf ) );
	CHECK( w.WriteBool( true ), "WriteBool true" );
	CHECK( w.WriteBool( false ), "WriteBool false" );
	CHECK( w.WriteUInt( 42, 6 ), "WriteUInt 6-bit" );
	CHECK( w.WriteInt( -17, 6 ), "WriteInt negative 6-bit" );
	CHECK( w.WriteInt( -2147483647 - 1, 32 ), "WriteInt INT_MIN 32-bit" );
	CHECK( w.WriteFloat( 3.25f ), "WriteFloat" );
	CHECK( w.WriteDouble( -123456.789 ), "WriteDouble" );
	CHECK( w.WriteString( "hello xglua" ), "WriteString" );
	CHECK( w.WriteVec3( 1.5f, -2.5f, 3.5f ), "WriteVec3" );
	const uint8_t raw[3] = { 0xDE, 0xAD, 0xBE };
	CHECK( w.WriteBytes( raw, 3 ), "WriteBytes" );
	CHECK_EQ_BOOL( w.overflow, false, "writer not overflowed" );

	XGLuaNetReader r;
	r.Init( buf, w.BytesWritten() );
	CHECK_EQ_BOOL( r.ReadBool(), true, "ReadBool true" );
	CHECK_EQ_BOOL( r.ReadBool(), false, "ReadBool false" );
	CHECK_EQ_INT( r.ReadUInt( 6 ), 42, "ReadUInt 6-bit" );
	CHECK_EQ_INT( r.ReadInt( 6 ), -17, "ReadInt negative 6-bit" );
	CHECK_EQ_INT( r.ReadInt( 32 ), -2147483648LL, "ReadInt INT_MIN 32-bit" );
	CHECK_EQ_FLT( r.ReadFloat(), 3.25, "ReadFloat" );
	CHECK_EQ_FLT( r.ReadDouble(), -123456.789, "ReadDouble" );
	char str[64] = { 0 };
	CHECK( r.ReadString( str, sizeof( str ) ), "ReadString" );
	CHECK_EQ_STR( str, "hello xglua", "ReadString value" );
	float x = 0, y = 0, z = 0;
	CHECK( r.ReadVec3( &x, &y, &z ), "ReadVec3" );
	CHECK_EQ_FLT( x, 1.5, "ReadVec3.x" );
	CHECK_EQ_FLT( y, -2.5, "ReadVec3.y" );
	CHECK_EQ_FLT( z, 3.5, "ReadVec3.z" );
	uint8_t rawOut[3] = { 0 };
	CHECK( r.ReadBytes( rawOut, 3 ), "ReadBytes" );
	CHECK_EQ_INT( rawOut[0], 0xDE, "ReadBytes[0]" );
	CHECK_EQ_INT( rawOut[2], 0xBE, "ReadBytes[2]" );
	CHECK_EQ_BOOL( r.underflow, false, "reader not underflowed" );
}

TEST(NetCodec_overflow_and_underflow)
{
	uint8_t tiny[2];
	XGLuaNetWriter w;
	w.Init( tiny, sizeof( tiny ) );
	CHECK( w.WriteUInt( 0xFFFF, 16 ), "exact fit 16 bits" );
	CHECK_EQ_BOOL( w.WriteBool( true ), false, "17th bit overflows" );
	CHECK_EQ_BOOL( w.overflow, true, "overflow flag set" );

	XGLuaNetReader r;
	r.Init( tiny, sizeof( tiny ) );
	CHECK_EQ_INT( r.ReadUInt( 16 ), 0xFFFF, "read 16 bits" );
	(void)r.ReadBool();
	CHECK_EQ_BOOL( r.underflow, true, "17th bit sets underflow" );
}

TEST(NetFrag_reassembly_and_seq_reset)
{
	XGLuaNetReassembler re;
	re.Reset();

	// Out-of-order 3-fragment message
	const uint8_t p0[3] = { 'A', 'B', 'C' };
	const uint8_t p1[3] = { 'D', 'E', 'F' };
	const uint8_t p2[2] = { 'G', 'H' };
	std::vector<uint8_t> out;

	XGLuaNetFragHeader h2 = { 0, 10, 5, 2, 3 };
	XGLuaNetFragHeader h0 = { 0, 10, 5, 0, 3 };
	XGLuaNetFragHeader h1 = { 0, 10, 5, 1, 3 };

	CHECK_EQ_BOOL( re.PushFragment( h2, p2, 2, &out ), false, "frag 2/3 pending" );
	CHECK_EQ_BOOL( re.PushFragment( h0, p0, 3, &out ), false, "frag 0/3 pending" );
	CHECK_EQ_BOOL( re.PushFragment( h1, p1, 3, &out ), true, "frag 1/3 completes" );
	CHECK_EQ_INT( (int)out.size(), 8, "reassembled length" );
	CHECK( std::memcmp( out.data(), "ABCDEFGH", 8 ) == 0, "reassembled bytes match" );

	// New seq resets incomplete prior message
	XGLuaNetFragHeader stale = { 0, 11, 5, 0, 2 };
	XGLuaNetFragHeader fresh = { 0, 12, 5, 0, 1 };
	CHECK_EQ_BOOL( re.PushFragment( stale, p0, 3, &out ), false, "seq 11 incomplete" );
	CHECK_EQ_BOOL( re.PushFragment( fresh, p2, 2, &out ), true, "seq 12 resets and completes" );
	CHECK_EQ_INT( (int)out.size(), 2, "seq 12 length" );
}

TEST(NetFrag_token_bucket_and_base64)
{
	XGLuaNetBucket b;
	b.Init( 100.0, 200.0, 0.0 );
	CHECK_EQ_BOOL( b.TryConsume( 150.0, 0.0 ), true, "burst consume 150" );
	CHECK_EQ_BOOL( b.TryConsume( 60.0, 0.0 ), false, "only 50 left -> denied" );
	CHECK_EQ_BOOL( b.TryConsume( 60.0, 0.5 ), true, "0.5s refill (+50 -> 100) -> allowed" );

	const uint8_t src[5] = { 0x00, 0x10, 0x83, 0x10, 0xFF };
	char enc[32] = { 0 };
	size_t encLen = XGLua_Base64Encode( src, sizeof( src ), enc, sizeof( enc ) );
	CHECK( encLen > 0, "base64 encode ok" );
	uint8_t dec[32] = { 0 };
	size_t decLen = 0;
	CHECK_EQ_BOOL( XGLua_Base64Decode( enc, dec, sizeof( dec ), &decLen ), true, "base64 decode ok" );
	CHECK_EQ_INT( (int)decLen, 5, "base64 decoded len" );
	CHECK( std::memcmp( src, dec, 5 ) == 0, "base64 roundtrip exact" );
	CHECK_EQ_BOOL( XGLua_Base64Decode( "!!!", dec, sizeof( dec ), &decLen ), false, "base64 rejects invalid" );
}

int main()
{
	return xtest::RunAll( "net_codec" );
}
