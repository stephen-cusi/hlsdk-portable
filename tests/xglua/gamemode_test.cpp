// Regression for the server gamemode loading chain
// (dlls/lua/xglua_gamemode.h + lua/includes/modules/gamemode.lua).
// Links the real ARM64 LuaJIT (lua_shared) and drives the production loader
// over an in-memory gamemodes/ tree; the real Lua module file is loaded
// from the repository (not a copy), so Register/Get/GetAll behavior is the
// shipped one. Every scenario asserts the Lua stack top and the stack
// sentinel survive; no test Lua lives under the runtime autorun.
//
// Build (ARM64, native; place the exe beside lua_shared.dll and make sure
// VCRUNTIME140.dll is installed or available beside the exe):
//   cl /nologo /EHsc /std:c++17 /utf-8 /I<luajit>/src /I<repo>/dlls/lua ^
//      /Fe:<luajit>/dist/arm64/gamemode_test.exe gamemode_test.cpp ^
//      /link /LIBPATH:<luajit>/dist/arm64 lua_shared.lib
// Run from <repo>/tests/xglua so the real module path below resolves.

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
// in-memory gamemodes/ tree
//===========================================================================

struct GMFile { const char *rel; const char *content; };

struct GMFS
{
	const GMFile *files;
	int count;
	int reads;
};

static XGLuaReadStatus GMMemRead( void *ctx, const char *rel, char **out, size_t *len )
{
	GMFS *fs = (GMFS *)ctx;
	++fs->reads;
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

static bool gmIsNil( lua_State *L )
{
	lua_getglobal( L, "GM" );
	bool a = lua_isnil( L, -1 );
	lua_pop( L, 1 );
	lua_getglobal( L, "GAMEMODE" );
	bool b = lua_isnil( L, -1 );
	lua_pop( L, 1 );
	return a && b;
}

// lua_getglobal with _G metamethods possible in hostile scenarios: tolerate
// non-table pushes by falling back to nil checks through plain type reads.
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

	// Load the real module from the repository tree.
	if( luaL_loadfile( L, "../../lua/includes/modules/gamemode.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "FAIL: module load: %s\n", lua_tostring( L, -1 ) );
		return 1;
	}
	lua_getglobal( L, "gamemode" );
	if( !lua_istable( L, -1 ) ) return fail( "gamemode global missing" );
	lua_pop( L, 1 );

	const int baseline = lua_gettop( L );

	//-----------------------------------------------------------------------
	// 1. no selection: loader not invoked semantics, nothing touched
	//-----------------------------------------------------------------------
	std::printf( "PHASE no-selection\n" ); std::fflush( stdout );
	{
		GMFS fs = { NULL, 0, 0 };
		char err[256];
		GMFS *fsp = &fs;
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "", err, sizeof( err ) );
		CHECK( r == XGLUA_GM_NO_SELECTION, "empty selection result" );
		r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, NULL, err, sizeof( err ) );
		CHECK( r == XGLUA_GM_NO_SELECTION, "null selection result" );
		CHECK( fs.reads == 0, "no-selection touched the filesystem" );
		CHECK( gmIsNil( L ), "no-selection set globals" );
		CHECK( sentinelOk( L, baseline ), "no-selection stack" );
	}

	//-----------------------------------------------------------------------
	// 2. invalid names: rejected before any file read (traversal guard)
	//-----------------------------------------------------------------------
	std::printf( "PHASE bad-names\n" ); std::fflush( stdout );
	{
		const char *bad[] = { "../valve", "a/b", "a\\b", "a.b", "sp ace", "..", "a:b", NULL };
		GMFS fs = { NULL, 0, 0 };
		luaL_dostring( L, "GM = { Stale = true }; GAMEMODE = GM" );
		for( int i = 0; bad[i]; ++i )
		{
			char err[256];
			GMFS *fsp = &fs;
			XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, bad[i], err, sizeof( err ) );
			CHECK( r == XGLUA_GM_BAD_NAME, "bad name accepted" );
		}
		char longname[XGLUA_GM_NAME_MAX + 8];
		std::memset( longname, 'a', sizeof( longname ) - 1 );
		longname[sizeof( longname ) - 1] = '\0';
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, longname, err, sizeof( err ) ) == XGLUA_GM_BAD_NAME,
			"oversized name accepted" );
		CHECK( fs.reads == 0, "bad names touched the filesystem" );
		CHECK( gmIsNil( L ), "invalid name left stale gamemode globals" );
		CHECK( sentinelOk( L, baseline ), "bad-names stack" );
	}

	//-----------------------------------------------------------------------
	// 3. simple mode, no base: shared then init, GM -> GAMEMODE, registered
	//-----------------------------------------------------------------------
	std::printf( "PHASE simple\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/solo/solo.txt",
			  "\"solo\"\n{\n    \"base\"  \"\"\n    \"base\"  \"ignored-duplicate\"\n    \"title\" \"A much longer gamemode title with \\\"quoted\\\" words\"\n}\n" },
			{ "gamemodes/solo/gamemode/shared.lua",
			  "Order[#Order + 1] = 'shared:solo'\nGM.Name = 'Solo'\nGM.Shared = true\nGM.AliasVisible = rawequal( GM, GAMEMODE )\n" },
			{ "gamemodes/solo/gamemode/init.lua",
			  "Order[#Order + 1] = 'init:solo'\nGM.Init = true\n" },
		};
		luaL_dostring( L, "Order = {}" );
		GMFS fs = { files, 3, 0 };
		char err[256];
		GMFS *fsp = &fs;
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "Solo", err, sizeof( err ) );
		CHECK( r == XGLUA_GM_OK, "simple mode result" );
		lua_getglobal( L, "GAMEMODE" );
		CHECK( lua_istable( L, -1 ), "GAMEMODE table" );
		lua_getfield( L, -1, "Name" );
		CHECK( lua_isstring( L, -1 ) && std::strcmp( lua_tostring( L, -1 ), "Solo" ) == 0, "GAMEMODE.Name" );
		lua_pop( L, 1 );
		lua_getfield( L, -1, "Shared" );
		CHECK( lua_toboolean( L, -1 ), "shared fields visible in init" );
		lua_pop( L, 2 );
		lua_getglobal( L, "GAMEMODE" );
		lua_getfield( L, -1, "AliasVisible" );
		CHECK( lua_toboolean( L, -1 ), "GAMEMODE aliases GM during script execution" );
		lua_pop( L, 2 );
		// registered under the same identity
		luaL_dostring( L, "Identical = rawequal( GAMEMODE, gamemode.Get( 'solo' ) )" );
		lua_getglobal( L, "Identical" );
		CHECK( lua_toboolean( L, -1 ), "Get returns the installed table" );
		lua_pop( L, 1 );
		// GetAll contains it under the same identity
		luaL_dostring( L, "AllOk = rawequal( gamemode.GetAll()[ 'solo' ], GAMEMODE )" );
		lua_getglobal( L, "AllOk" );
		CHECK( lua_toboolean( L, -1 ), "GetAll contains the installed table" );
		lua_pop( L, 1 );
		// order: shared before init
		luaL_dostring( L, "OrderOk = Order[1] == 'shared:solo' and Order[2] == 'init:solo' and #Order == 2" );
		lua_getglobal( L, "OrderOk" );
		CHECK( lua_toboolean( L, -1 ), "shared-then-init order" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "simple stack" );
		// reset globals for the next scenario
		luaL_dostring( L, "GM, GAMEMODE = nil, nil" );
	}

	//-----------------------------------------------------------------------
	// 4. base chain: base first, derived second, BaseClass identity
	//-----------------------------------------------------------------------
	std::printf( "PHASE base-chain\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/base/base.txt",
			  "\"base\"\n{\n    \"title\" \"Base\"\n}\n" },	// no base key: root
			{ "gamemodes/base/gamemode/shared.lua",
			  "Order[#Order + 1] = 'shared:base'\nGM.Name = 'Base'\nGM.BaseMark = 'b'\n" },
			{ "gamemodes/base/gamemode/init.lua",
			  "Order[#Order + 1] = 'init:base'\n" },
			{ "gamemodes/der/der.txt",
			  "\"der\"\n{\n    \"base\"  \"BASE\"\n}\n" },
			{ "gamemodes/der/gamemode/shared.lua",
			  "Order[#Order + 1] = 'shared:der'\nGM.Name = 'Derived'\n" },
			{ "gamemodes/der/gamemode/init.lua",
			  "Order[#Order + 1] = 'init:der'\nGM.DerInit = true\n" },
		};
		luaL_dostring( L, "Order = {}" );
		GMFS fs = { files, 6, 0 };
		char err[256];
		GMFS *fsp = &fs;
		XGLua_GMResult r = XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "der", err, sizeof( err ) );
		CHECK( r == XGLUA_GM_OK, "chain result" );
		luaL_dostring( L,
			"ChainOk = Order[1] == 'shared:base' and Order[2] == 'init:base'\n"
			"    and Order[3] == 'shared:der' and Order[4] == 'init:der' and #Order == 4\n"
			"IdentityOk = rawequal( GAMEMODE, gamemode.Get( 'der' ) )\n"
			"    and rawequal( GAMEMODE.BaseClass, gamemode.Get( 'base' ) )\n"
			"FreshOk = GAMEMODE.BaseMark == nil and GAMEMODE.BaseClass.BaseMark == 'b'\n"
			"FieldsOk = GAMEMODE.Name == 'Derived' and GAMEMODE.DerInit == true\n"
			"RootKept = gamemode.Get( 'base' ).Name == 'Base'" );
		lua_getglobal( L, "ChainOk" );
		CHECK( lua_toboolean( L, -1 ), "base-first execution order" );
		lua_pop( L, 1 );
		lua_getglobal( L, "IdentityOk" );
		CHECK( lua_toboolean( L, -1 ), "BaseClass identity" );
		lua_pop( L, 1 );
		lua_getglobal( L, "FreshOk" );
		CHECK( lua_toboolean( L, -1 ), "derived GM is fresh (no field bleed)" );
		lua_pop( L, 1 );
		lua_getglobal( L, "FieldsOk" );
		CHECK( lua_toboolean( L, -1 ), "derived fields" );
		lua_pop( L, 1 );
		lua_getglobal( L, "RootKept" );
		CHECK( lua_toboolean( L, -1 ), "base still registered" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "chain stack" );
		luaL_dostring( L, "GM, GAMEMODE = nil, nil" );
	}

	//-----------------------------------------------------------------------
	// 5. cycles: direct and mutual
	//-----------------------------------------------------------------------
	std::printf( "PHASE cycles\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/selfy/selfy.txt", "\"selfy\"\n{\n    \"base\"  \"selfy\"\n}\n" },
			{ "gamemodes/a/a.txt", "\"a\"\n{\n    \"base\"  \"b\"\n}\n" },
			{ "gamemodes/b/b.txt", "\"b\"\n{\n    \"base\"  \"a\"\n}\n" },
		};
		GMFS fs = { files, 3, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "selfy", err, sizeof( err ) ) == XGLUA_GM_CYCLE,
			"self cycle accepted" );
		CHECK( std::strstr( err, "cycle" ), "self cycle diagnostic" );
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "a", err, sizeof( err ) ) == XGLUA_GM_CYCLE,
			"mutual cycle accepted" );
		CHECK( std::strstr( err, "cycle" ), "mutual cycle diagnostic" );
		CHECK( gmIsNil( L ), "cycle left globals" );
		CHECK( sentinelOk( L, baseline ), "cycles stack" );
	}

	//-----------------------------------------------------------------------
	// 6. missing metadata / missing base / missing script / bad metadata
	//-----------------------------------------------------------------------
	std::printf( "PHASE missing\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/ghostbase/ghostbase.txt", "\"ghostbase\"\n{\n    \"base\"  \"nosuch\"\n}\n" },
			{ "gamemodes/noshared/noshared.txt", "\"noshared\"\n{\n}\n" },
			{ "gamemodes/noshared/gamemode/init.lua", "Order[#Order + 1] = 'x'\n" },
			{ "gamemodes/badmeta/badmeta.txt", "\"badmeta\"\n{\n    \"base\"  \"unterminated\n}\n" },
			{ "gamemodes/badmeta/gamemode/shared.lua", "" },
			{ "gamemodes/badmeta/gamemode/init.lua", "" },
			{ "gamemodes/trailing/trailing.txt", "\"trailing\"\n{\n}\ntrailing-junk\n" },
		};
		GMFS fs = { files, 7, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "nothing", err, sizeof( err ) ) == XGLUA_GM_MISSING_META,
			"missing requested metadata accepted" );
		CHECK( std::strstr( err, "gamemodes/nothing/nothing.txt" ), "missing meta names the file" );
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "ghostbase", err, sizeof( err ) ) == XGLUA_GM_MISSING_META,
			"missing base metadata accepted" );
		CHECK( std::strstr( err, "nosuch" ), "missing base names the base" );
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "noshared", err, sizeof( err ) ) == XGLUA_GM_MISSING_SCRIPT,
			"missing shared.lua accepted" );
		CHECK( std::strstr( err, "shared.lua" ), "missing script names the file" );
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "badmeta", err, sizeof( err ) ) == XGLUA_GM_BAD_META,
			"malformed metadata accepted" );
		CHECK( std::strstr( err, "line" ), "malformed metadata has a line hint" );
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "trailing", err, sizeof( err ) ) == XGLUA_GM_BAD_META,
			"trailing metadata accepted" );
		CHECK( gmIsNil( L ), "missing left globals" );
		CHECK( sentinelOk( L, baseline ), "missing stack" );
	}

	//-----------------------------------------------------------------------
	// 7. script error: reported, globals cleared, server keeps running
	//-----------------------------------------------------------------------
	std::printf( "PHASE script-error\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/boom/boom.txt", "\"boom\"\n{\n}\n" },
			{ "gamemodes/boom/gamemode/shared.lua", "GM.Name = 'Boom'\nerror( 'expected gm failure' )\n" },
			{ "gamemodes/boom/gamemode/init.lua", "Order[#Order + 1] = 'should-not-run'\n" },
		};
		GMFS fs = { files, 3, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "boom", err, sizeof( err ) ) == XGLUA_GM_SCRIPT_ERROR,
			"script error result" );
		CHECK( std::strstr( err, "expected gm failure" ), "script error message" );
		CHECK( std::strstr( err, "shared.lua" ), "script error names the file" );
		CHECK( gmIsNil( L ), "script error left globals" );
		luaL_dostring( L, "NotRegistered = gamemode.Get( 'boom' ) == nil" );
		lua_getglobal( L, "NotRegistered" );
		CHECK( lua_toboolean( L, -1 ), "failed mode not registered" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "script-error stack" );
	}

	//-----------------------------------------------------------------------
	// 8. derived script error: base stays registered and usable
	//-----------------------------------------------------------------------
	std::printf( "PHASE partial-chain\n" ); std::fflush( stdout );
	{
		static const GMFile files[] = {
			{ "gamemodes/base2/base2.txt", "\"base2\"\n{\n}\n" },
			{ "gamemodes/base2/gamemode/shared.lua", "GM.Name = 'Base2'\n" },
			{ "gamemodes/base2/gamemode/init.lua", "" },
			{ "gamemodes/der2/der2.txt", "\"der2\"\n{\n    \"base\"  \"base2\"\n}\n" },
			{ "gamemodes/der2/gamemode/shared.lua", "error( 'derived blows up' )\n" },
			{ "gamemodes/der2/gamemode/init.lua", "" },
		};
		GMFS fs = { files, 6, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "der2", err, sizeof( err ) ) == XGLUA_GM_SCRIPT_ERROR,
			"derived error result" );
		CHECK( std::strstr( err, "derived blows up" ), "derived error message" );
		CHECK( gmIsNil( L ), "derived error left globals" );
		luaL_dostring( L, "BaseIntact = gamemode.Get( 'base2' ) ~= nil and gamemode.Get( 'base2' ).Name == 'Base2'" );
		lua_getglobal( L, "BaseIntact" );
		CHECK( lua_toboolean( L, -1 ), "base survived derived failure" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "partial-chain stack" );
	}

	//-----------------------------------------------------------------------
	// 9. chain depth cap
	//-----------------------------------------------------------------------
	std::printf( "PHASE depth\n" ); std::fflush( stdout );
	{
		// g00 (root) <- g01 <- ... <- g17: 18 modes, over the cap of 16
		static char storage[18 * 3][128];
		static GMFile files[18 * 3];
		char name[8], base[8];
		int n = 0;
		for( int i = 0; i < 18; ++i )
		{
			std::snprintf( name, sizeof( name ), "g%02d", i );
			std::snprintf( storage[i * 3 + 0], 128, "gamemodes/%s/%s.txt", name, name );
			std::snprintf( storage[i * 3 + 1], 128, "gamemodes/%s/gamemode/shared.lua", name );
			std::snprintf( storage[i * 3 + 2], 128, "gamemodes/%s/gamemode/init.lua", name );
			files[n].rel = storage[i * 3 + 0];
			files[n].content = "";	// real content installed below
			++n;
			files[n].rel = storage[i * 3 + 1];
			files[n].content = "";
			++n;
			files[n].rel = storage[i * 3 + 2];
			files[n].content = "";
			++n;
		}
		// txt contents need per-mode base; build them into a second buffer
		static char txt[18][64];
		for( int i = 0; i < 18; ++i )
		{
			std::snprintf( name, sizeof( name ), "g%02d", i );
			std::snprintf( base, sizeof( base ), "g%02d", i - 1 );
			std::snprintf( txt[i], sizeof( txt[i] ),
				i == 0 ? "\"g00\"\n{\n}\n" : "\"%s\"\n{\n    \"base\"  \"%s\"\n}\n", name, base );
			files[i * 3 + 0].content = txt[i];
		}
		GMFS fs = { files, 18 * 3, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "g17", err, sizeof( err ) ) == XGLUA_GM_TOO_DEEP,
			"depth cap not enforced" );
		CHECK( gmIsNil( L ), "depth left globals" );
		CHECK( sentinelOk( L, baseline ), "depth stack" );
	}

	//-----------------------------------------------------------------------
	// 10. empty gamemodes tree = plain missing metadata, still diagnosable
	//-----------------------------------------------------------------------
	std::printf( "PHASE empty-tree\n" ); std::fflush( stdout );
	{
		GMFS fs = { NULL, 0, 0 };
		char err[256];
		GMFS *fsp = &fs;
		CHECK( XGLua_LoadGamemodeChain( L, GMMemRead, fsp, "anything", err, sizeof( err ) ) == XGLUA_GM_MISSING_META,
			"empty tree result" );
		CHECK( gmIsNil( L ), "empty tree left globals" );
		CHECK( sentinelOk( L, baseline ), "empty tree stack" );
	}

	//-----------------------------------------------------------------------
	// 11. gamemode module API used standalone (production gamemode.lua)
	//-----------------------------------------------------------------------
	std::printf( "PHASE module-api\n" ); std::fflush( stdout );
	{
		luaL_dostring( L, "t1 = gamemode.Register( { Mark = 1 }, 'MixedCase' )" );
		luaL_dostring( L, "t2 = gamemode.Get( 'mixedcase' )" );
		lua_getglobal( L, "t1" );
		lua_getglobal( L, "t2" );
		CHECK( lua_istable( L, -1 ) && lua_istable( L, -2 ) && lua_rawequal( L, -1, -2 ),
			"Register/Get case-insensitive identity" );
		lua_pop( L, 2 );
		luaL_dostring( L, "okAll = gamemode.GetAll()[ 'mixedcase' ] ~= nil and gamemode.GetAll()[ 'solo' ] ~= nil" );
		lua_getglobal( L, "okAll" );
		CHECK( lua_toboolean( L, -1 ), "GetAll snapshot contents" );
		lua_pop( L, 1 );
		luaL_dostring( L, "noNil = gamemode.Get( 'never' ) == nil and gamemode.Get( nil ) == nil" );
		lua_getglobal( L, "noNil" );
		CHECK( lua_toboolean( L, -1 ), "Get unknown/nil" );
		lua_pop( L, 1 );
		CHECK( sentinelOk( L, baseline ), "module-api stack" );
	}

	CHECK( sentinelOk( L, baseline ), "final sentinel" );
	lua_close( L );
	std::puts( "gamemode loading chain OK" );
	std::fflush( stdout );
	return 0;
}
