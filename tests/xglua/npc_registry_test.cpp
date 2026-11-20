// Regression for lua/includes/modules/npc.lua (M7-7D).
// Build with /I<repo>/common and run from tests/xglua.

#define HSPRITE XGLUA_TEST_WIN_HSPRITE
#include "test_framework.h"
#undef HSPRITE

static bool SetupNPCTest( lua_State *L )
{
	lua_pushboolean( L, 1 ); lua_setglobal( L, "SERVER" );
	if( luaL_loadfile( L, "../../lua/includes/modules/npc.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: npc.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

TEST(NPC_register_inheritance_and_cycle_guard)
{
	LUA(
		"local base = { Health = 150, CustomTag = 'elite' }\n"
		"npc.Register( base, 'npc_elite_base' )\n"
		"local child = { Base = 'npc_elite_base', Model = 'models/barney.mdl' }\n"
		"npc.Register( child, 'NPC_Elite_Guard' )\n"
		"-- Cyclic chain must not hang\n"
		"npc.Register( { Base = 'npc_cycle_b' }, 'npc_cycle_a' )\n"
		"npc.Register( { Base = 'npc_cycle_a' }, 'npc_cycle_b' )\n"
	);

	CHECK( !::xtest::RunLua( L, "assert(npc.Get('npc_elite_guard') ~= nil)", __FILE__, __LINE__ ) == false, "normalized lowercase lookup" );
	CHECK_LUA_INT( "npc.Get('npc_elite_guard').Health", 150, "inherited Health from Base" );
	CHECK_LUA_STR( "npc.Get('npc_elite_guard').CustomTag", "elite", "inherited CustomTag from Base" );
	CHECK_LUA_BOOL( "npc.Get('npc_elite_guard').Abstract", false, "concrete NPC defaults Abstract=false" );
	CHECK_LUA_BOOL( "npc.Get('npc_cycle_a').MissingField == nil", true, "cyclic Base terminates cleanly" );
}

int main()
{
	xtest::Setup() = SetupNPCTest;
	return xtest::RunAll( "npc_registry" );
}
