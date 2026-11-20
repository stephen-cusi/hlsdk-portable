// Regression for common/lua/xglua_cl_swep_meta.h and XGLUA_NET_FLAG_SWEPLIST.
// Build with /I<repo>/common and run from tests/xglua.

#include "test_framework.h"
#include "lua/xglua_cl_swep_meta.h"
#include "lua/xglua_netfrag.h"

TEST(CLSWEPRegistry_encode_decode_and_lookup)
{
	XGLuaCLSWEPRegistry sv, cl;

	XGLuaCLSWEPMeta w1;
	std::memset( &w1, 0, sizeof( w1 ) );
	w1.id = 17;
	std::strcpy( w1.classname, "weapon_xgl_carbine" );
	w1.slot = 2;
	w1.slotpos = 2;
	w1.primaryAutomatic = true;
	w1.secondaryAutomatic = false;
	w1.clipsize = 30;
	sv.Upsert( w1 );

	XGLuaCLSWEPMeta w2;
	std::memset( &w2, 0, sizeof( w2 ) );
	w2.id = 18;
	std::strcpy( w2.classname, "weapon_xgl_dualammo" );
	w2.slot = 2;
	w2.slotpos = 3;
	w2.primaryAutomatic = false;
	w2.secondaryAutomatic = true;
	w2.clipsize = 12;
	sv.Upsert( w2 );

	uint8_t buf[512];
	XGLuaNetWriter writer;
	writer.Init( buf, sizeof( buf ) );
	CHECK( sv.EncodeAll( writer ), "EncodeAll succeeds" );

	XGLuaNetReader reader;
	reader.Init( buf, writer.BytesWritten() );
	CHECK( cl.DecodeAll( reader, true ), "DecodeAll succeeds" );

	const XGLuaCLSWEPMeta *got1 = cl.FindById( 17 );
	CHECK( got1 != NULL, "FindById(17) found" );
	CHECK_EQ_STR( got1->classname, "weapon_xgl_carbine", "classname matches" );
	CHECK_EQ_INT( got1->slot, 2, "slot matches" );
	CHECK_EQ_INT( got1->slotpos, 2, "slotpos matches" );
	CHECK_EQ_BOOL( got1->primaryAutomatic, true, "primaryAutomatic matches" );
	CHECK_EQ_INT( got1->clipsize, 30, "clipsize matches" );

	const XGLuaCLSWEPMeta *got2 = cl.FindByClass( "weapon_xgl_dualammo" );
	CHECK( got2 != NULL, "FindByClass found" );
	CHECK_EQ_INT( got2->id, 18, "id matches" );
	CHECK_EQ_BOOL( got2->secondaryAutomatic, true, "secondaryAutomatic matches" );

	CHECK( cl.FindById( 999 ) == NULL, "unknown id returns NULL" );
}

int main()
{
	return xtest::RunAll( "cl_swep_meta" );
}
