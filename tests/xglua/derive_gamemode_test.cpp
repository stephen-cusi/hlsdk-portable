// Regression for the global DeriveGamemode(name)
// (lua/includes/modules/gamemode.lua + interaction with the C++ loading
// chain in dlls/lua/xglua_gamemode.h). Loads the real module file from the
// repository, so the tested behavior is the shipped one. No test Lua lives
// under the runtime autorun.
//
// Covered:
//   - registered base: GM.BaseClass set to the exact registered table
//   - case-insensitive lookup through gamemode.Get normalization
//   - repeated calls are idempotent; same-table no-op after the loader
//     already installed BaseClass from the txt chain
//   - unknown base and GM-not-a-table raise Lua errors naming the mode
//   - the real loading chain with DeriveGamemode inside shared.lua works
//     and its failure policy is unchanged (script error -> diagnosed skip)
//
// Build (ARM64, native; put the exe beside lua_shared.dll and make sure
// VCRUNTIME140.dll is installed or available beside the exe):
//   cl /nologo /EHsc /std:c++17 /utf-8 /I<luajit>/src /I<repo>/dlls/lua ^
//      /Fe:<luajit>/dist/arm64/derive_gamemode_test.exe derive_gamemode_test.cpp ^
//      /link /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
// Run from <repo>/tests/xglua so the module path below resolves.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <windows.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "xglua_gamemode.h"

//===========================================================================
// in-memory gamemodes/ tree (for the loader-integration phases)
//===========================================================================

struct GMFile { const char *rel; const char *content; };
struct GMFS { const GMFile *files; int count; };

static XGLuaReadStatus GMMemRead( void *ctx, const char *rel, char **out, size_t *len )
{
	GMFS *fs = (GMFS *)ctx;
	for( int i = 0; i < fs->count; ++i )
	{
		if( std::strcmp( fs->files[i].rel, rel ) == 0 )
		{
			size_t n = std::strlen( fs->files[i].content );
			char *data = (char *)std::malloc( n + 1 );
			if( !data ) return XGLUA_READ_ERROR;
			std::memcpy( data, fs->files[i].content, n + 1 );
			*out = data;
			*len = n;
			return XGLUA_READ_OK;
		}
	}
	return XGLUA_READ_MISSING;
}

//===========================================================================
// harness
//===========================================================================

static int fail( const char *what )
{
	std::printf( "FAIL: %s\n", what );
	std::fflush( stdout );
	return 1;
}

static DWORD WINAPI Watchdog( LPVOID )
{
	Sleep( 120000 );
	std::printf( "WATCHDOG: timeout after 120s\n" );
	std::fflush( stdout );
	_exit( 2 );
	return 0;
}

static bool sentinelOk( lua_State *L, int baseline )
{
	return lua_gettop( L ) == baseline && lua_isstring( L, 1 ) &&
		std::strcmp( lua_tostring( L, 1 ), "sentinel" ) == 0;
}

// pcall a snippet; returns false and copies the error message on failure.
static bool tryLua( lua_State *L, const char *code, char *err, size_t errlen )
{
	if( luaL_loadstring( L, code ) || lua_pcall( L, 0, 0, 0 ) )
	{
		const char *message = lua_tostring( L, -1 );
		snprintf( err, errlen, "%s", message ? message : "?" );
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

#define CHECK( cond, msg ) do { if( !( cond ) ) return fail( msg ); } while( 0 )

int main()
{
	std::printf( "SMOKE: harness alive\n" );
	std::fflush( stdout );
	CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );

	lua_State *L = luaL_newstate();
	if( !L ) return fail( "new state" );
	luaL_openlibs( L );
	lua_pushliteral( L, "sentinel" );

	if( luaL_loadfile( L, "../../lua/includes/modules/gamemode.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "FAIL: module load: %s\n", lua_tostring( L, -1 ) );
		return 1;
	}
	lua_getglobal( L, "DeriveGamemode" );
	if( !lua_isfunction( L, -1 ) ) return fail( "DeriveGamemode global missing" );
	lua_pop( L, 1 );

	const int baseline = lua_gettop( L );
	char err[256];

	//-----------------------------------------------------------------------
	// 1. registered base: BaseClass becomes exactly the registered table
	//-----------------------------------------------------------------------
	std::printf( "PHASE registered-base\n" ); std::fflush( stdout );
	{
		CHECK( tryLua( L, "GM = {}\ngamemode.Register( { Name = 'Base' }, 'base' )", err, sizeof( err ) ),
			"fixture setup" );
		CHECK( tryLua( L, "DeriveGamemode( 'base' )", err, sizeof( err ) ), "registered base raised" );
		luaL_dostring( L, "R = rawequal( GM.BaseClass, gamemode.Get( 'base' ) ) and GM.BaseClass.Name == 'Base'" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "BaseClass identity" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "registered-base stack" );
	}

	//-----------------------------------------------------------------------
	// 2. case-insensitive lookup
	//-----------------------------------------------------------------------
	std::printf( "PHASE case\n" ); std::fflush( stdout );
	{
		CHECK( tryLua( L, "DeriveGamemode( 'BASE' )", err, sizeof( err ) ), "uppercase raised" );
		CHECK( tryLua( L, "DeriveGamemode( 'BaSe' )", err, sizeof( err ) ), "mixed case raised" );
		luaL_dostring( L, "R = rawequal( GM.BaseClass, gamemode.Get( 'base' ) )" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "case normalization reaches the same table" );
		lua_pop( L, 1 );
	}

	//-----------------------------------------------------------------------
	// 3. repeated calls: idempotent, same table, no error
	//-----------------------------------------------------------------------
	std::printf( "PHASE idempotent\n" ); std::fflush( stdout );
	{
		for( int i = 0; i < 3; ++i )
			CHECK( tryLua( L, "DeriveGamemode( 'base' )", err, sizeof( err ) ), "repeat call raised" );
		luaL_dostring( L, "R = rawequal( GM.BaseClass, gamemode.Get( 'base' ) )" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "repeat keeps BaseClass" );
		lua_pop( L, 1 );
		// the registry is untouched by derive calls
		luaL_dostring( L, "R = gamemode.Get( 'ghost' ) == nil and gamemode.Get( 'base' ) ~= nil" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "registry untouched" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "idempotent stack" );
	}

	//-----------------------------------------------------------------------
	// 4. unknown base: error names the mode
	//-----------------------------------------------------------------------
	std::printf( "PHASE unknown-base\n" ); std::fflush( stdout );
	{
		CHECK( !tryLua( L, "DeriveGamemode( 'ghost' )", err, sizeof( err ) ), "unknown base accepted" );
		CHECK( std::strstr( err, "ghost" ), "unknown base error names the mode" );
		CHECK( std::strstr( err, "not registered" ), "unknown base error is explicit" );
		// the failed call leaves BaseClass untouched
		luaL_dostring( L, "R = rawequal( GM.BaseClass, gamemode.Get( 'base' ) )" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "failed derive keeps BaseClass" );
		lua_pop( L, 1 );
	}

	//-----------------------------------------------------------------------
	// 5. GM not initialized: error names the mode and says why
	//-----------------------------------------------------------------------
	std::printf( "PHASE gm-uninitialized\n" ); std::fflush( stdout );
	{
		CHECK( tryLua( L, "GM = nil", err, sizeof( err ) ), "fixture" );
		CHECK( !tryLua( L, "DeriveGamemode( 'solo' )", err, sizeof( err ) ), "nil GM accepted" );
		CHECK( std::strstr( err, "solo" ), "GM-nil error names the mode" );
		CHECK( std::strstr( err, "GM is not a table" ), "GM-nil error is explicit" );
		CHECK( tryLua( L, "GM = 42", err, sizeof( err ) ), "fixture" );
		CHECK( !tryLua( L, "DeriveGamemode( 'solo' )", err, sizeof( err ) ), "non-table GM accepted" );
		CHECK( std::strstr( err, "solo" ), "non-table GM error names the mode" );
		CHECK( !tryLua( L, "GM = {}; DeriveGamemode( nil )", err, sizeof( err ) ), "nil name accepted" );
		CHECK( std::strstr( err, "nil" ), "nil name reported" );
		CHECK( !tryLua( L, "GM = {}; DeriveGamemode( 7 )", err, sizeof( err ) ), "number name accepted" );
		CHECK( std::strstr( err, "7" ), "number name reported" );
		CHECK( !tryLua( L,
			"GM = {}; local bad = setmetatable( {}, { __tostring = function() error( 'must not stringify table' ) end } ); DeriveGamemode( bad )",
			err, sizeof( err ) ), "table name accepted" );
		CHECK( std::strstr( err, "<table>" ) && std::strstr( err, "got table" ),
			"table name error avoids __tostring" );
		CHECK( !std::strstr( err, "must not stringify table" ), "invalid argument invoked __tostring" );
		CHECK( sentinelOk( L, baseline ), "gm-uninitialized stack" );
	}

	//-----------------------------------------------------------------------
	// 6. real loading chain: DeriveGamemode inside shared.lua, uppercase
	//    name, loader already pre-installed BaseClass from the txt chain
	//-----------------------------------------------------------------------
	std::printf( "PHASE loader-integration\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/base3/base3.txt", "\"base3\"\n{\n}\n" },
			{ "gamemodes/base3/gamemode/shared.lua", "GM.Name = 'B3'\nGM.BaseMark = 'b3'\n" },
			{ "gamemodes/base3/gamemode/init.lua", "" },
			{ "gamemodes/der3/der3.txt", "\"der3\"\n{\n    \"base\"  \"base3\"\n}\n" },
			{ "gamemodes/der3/gamemode/shared.lua",
			  "GM.Name = 'D3'\nDeriveGamemode( 'BASE3' )\n" },
			{ "gamemodes/der3/gamemode/init.lua", "GM.Ok = true\n" },
		};
		GMFS fs = { files, 6 };
		GMFS *fsp = &fs;
		char gmerr[256];
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "der3", gmerr, sizeof( gmerr ) );
		CHECK( r == XGLUA_GM_OK, "chain with DeriveGamemode failed" );
		luaL_dostring( L,
			"R = GAMEMODE.Name == 'D3' and GAMEMODE.Ok == true\n"
			"    and rawequal( GAMEMODE.BaseClass, gamemode.Get( 'base3' ) )\n"
			"    and GAMEMODE.BaseClass.BaseMark == 'b3'" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "chain + derive integration" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "loader-integration stack" );
	}

	//-----------------------------------------------------------------------
	// 7. unknown base inside a gamemode script: unchanged failure policy
	//-----------------------------------------------------------------------
	std::printf( "PHASE loader-unknown-base\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/base4/base4.txt", "\"base4\"\n{\n}\n" },
			{ "gamemodes/base4/gamemode/shared.lua", "GM.Name = 'B4'\n" },
			{ "gamemodes/base4/gamemode/init.lua", "" },
			{ "gamemodes/der4/der4.txt", "\"der4\"\n{\n    \"base\"  \"base4\"\n}\n" },
			{ "gamemodes/der4/gamemode/shared.lua", "DeriveGamemode( 'ghost4' )\n" },
			{ "gamemodes/der4/gamemode/init.lua", "GM.Ran = true\n" },
		};
		GMFS fs = { files, 6 };
		GMFS *fsp = &fs;
		char gmerr[256];
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "der4", gmerr, sizeof( gmerr ) );
		CHECK( r == XGLUA_GM_SCRIPT_ERROR, "unknown base in script not a script error" );
		CHECK( std::strstr( gmerr, "ghost4" ), "script error names the mode" );
		CHECK( std::strstr( gmerr, "not registered" ), "script error is explicit" );
		luaL_dostring( L, "R = gamemode.Get( 'base4' ) ~= nil and gamemode.Get( 'base4' ).Name == 'B4'" );
		lua_getglobal( L, "R" );
		CHECK( lua_toboolean( L, -1 ), "base survived derived derive-failure" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "loader-unknown-base stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "DeriveGamemode OK" );
	std::fflush( stdout );
	return 0;
}
