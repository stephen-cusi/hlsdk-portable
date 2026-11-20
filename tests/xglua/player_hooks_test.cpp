// Regression for the PlayerSpawn / PlayerInitialSpawn / PlayerDisconnected /
// PlayerDeath dispatch family (dlls/lua/xglua_hook.h XGLua_HookRunProtected)
// plus the shipped hook.lua module behaviors (error auto-removal, mid-call
// snapshot, integer identifiers, multi-value returns). Loads the REAL
// lua/includes/modules/hook.lua; run from tests/xglua so the path resolves.

#include "test_framework.h"
#include "xglua_hook.h"

//---------------------------------------------------------------------------
// fixture: registry-ref entity stand-ins and error capture
//---------------------------------------------------------------------------
static int reportedErrors;
static char lastMessage[256];
static char lastContext[128];
static void onError( lua_State *L, const char *context )
{
	++reportedErrors;
	const char *message = XGLua_ErrorText( L, -1 );
	snprintf( lastMessage, sizeof( lastMessage ), "%s", message ? message : "" );
	snprintf( lastContext, sizeof( lastContext ), "%s", context ? context : "" );
	lua_pop( L, 1 );
}

static int playerRef, victimRef, inflictorRef, attackerRef;
static bool deathHasInflictor, deathHasAttacker;
static bool pusherArmed;

static int PushPlayer( lua_State *L, void * )
{
	if( pusherArmed ) return luaL_error( L, "pusher boom" );
	lua_rawgeti( L, LUA_REGISTRYINDEX, playerRef );
	return 1;
}
static void PushRefOrNull( lua_State *L, int ref, bool present )
{
	if( present ) lua_rawgeti( L, LUA_REGISTRYINDEX, ref );
	else lua_pushnil( L );
}
static int PushDeathArgs( lua_State *L, void * )
{
	PushRefOrNull( L, victimRef, true );
	PushRefOrNull( L, inflictorRef, deathHasInflictor );
	PushRefOrNull( L, attackerRef, deathHasAttacker );
	return 3;
}

static int fx_runspawn( lua_State *L ) { XGLua_HookRunProtected( L, "PlayerSpawn", PushPlayer, NULL, onError ); return 0; }
static int fx_runinitial( lua_State *L ) { XGLua_HookRunProtected( L, "PlayerInitialSpawn", PushPlayer, NULL, onError ); return 0; }
static int fx_rundisconnected( lua_State *L ) { XGLua_HookRunProtected( L, "PlayerDisconnected", PushPlayer, NULL, onError ); return 0; }
static int fx_rundeath( lua_State *L ) { XGLua_HookRunProtected( L, "PlayerDeath", PushDeathArgs, NULL, onError ); return 0; }
static int fx_deathargs( lua_State *L ) { deathHasInflictor = lua_toboolean( L, 1 ); deathHasAttacker = lua_toboolean( L, 2 ); return 0; }
static int fx_armpusher( lua_State *L ) { pusherArmed = lua_toboolean( L, 1 ); return 0; }
static int fx_errors( lua_State *L ) { lua_pushinteger( L, reportedErrors ); lua_pushstring( L, lastMessage ); lua_pushstring( L, lastContext ); return 3; }
static int fx_throwingindex( lua_State *L ) { return luaL_error( L, "global lookup boom" ); }

static bool SetupPlayerHooks( lua_State *L )
{
	reportedErrors = 0; lastMessage[0] = '\0'; lastContext[0] = '\0';
	deathHasInflictor = deathHasAttacker = true;
	pusherArmed = false;
	if( luaL_loadfile( L, "../../lua/includes/modules/hook.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: hook.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	// Capture hook.lua's containment prints instead of letting them spam.
	if( luaL_dostring( L,
		"PrintLog = {}\n"
		"print = function( ... )\n"
		"	local t = {}\n"
		"	for i = 1, select( '#', ... ) do t[#t + 1] = tostring( select( i, ... ) ) end\n"
		"	PrintLog[#PrintLog + 1] = table.concat( t, ' ' )\n"
		"end" ) )
		return false;
	static const char *tags[] = { "player", "victim", "inflictor", "attacker" };
	static const char *globalNames[] = { "PlayerFixture", "VictimFixture", "InflictorFixture", "AttackerFixture" };
	int *refs[] = { &playerRef, &victimRef, &inflictorRef, &attackerRef };
	for( int i = 0; i < 4; ++i )
	{
		char code[64];
		snprintf( code, sizeof( code ), "return { tag = '%s' }", tags[i] );
		if( luaL_dostring( L, code ) ) return false;
		*refs[i] = luaL_ref( L, LUA_REGISTRYINDEX );
		lua_rawgeti( L, LUA_REGISTRYINDEX, *refs[i] );
		lua_setglobal( L, globalNames[i] );
	}
	static const luaL_Reg helpers[] = {
		{ "runspawn", fx_runspawn }, { "runinitial", fx_runinitial },
		{ "rundisconnected", fx_rundisconnected }, { "rundeath", fx_rundeath },
		{ "deathargs", fx_deathargs }, { "armpusher", fx_armpusher }, { "errors", fx_errors },
		{ NULL, NULL } };
	for( const luaL_Reg *f = helpers; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setglobal( L, f->name ); }
	return true;
}

// Pins: no hook and no GAMEMODE is a silent no-op for every spawn event.
TEST(Spawn_missing_silent)
{
	LUA( "runspawn(); runinitial(); rundisconnected(); rundeath()" );
	CHECK_LUA_INT( "select(1, errors())", 0, "HookRunProtected: missing handlers stay silent" );
}

// Pins: PlayerSpawn delivers the exact player to hooks and the GM fallback.
TEST(Spawn_player_arg)
{
	LUA( "hook.Add('PlayerSpawn', 'rec', function(ply) HookPly = ply end); runspawn()" );
	CHECK_LUA_BOOL( "rawequal(HookPly, PlayerFixture)", true, "PlayerSpawn: hook receives the player" );
	LUA( "hook.Remove('PlayerSpawn', 'rec')\n"
		"GAMEMODE = { PlayerSpawn = function(gm, ply) GMSelf = rawequal(gm, GAMEMODE); GMPly = ply end }\n"
		"runspawn()" );
	CHECK_LUA_BOOL( "GMSelf", true, "PlayerSpawn: GM called with itself" );
	CHECK_LUA_BOOL( "rawequal(GMPly, PlayerFixture)", true, "PlayerSpawn: GM fallback receives the player" );
}

// Pins: PlayerInitialSpawn and PlayerSpawn never cross-fire.
TEST(InitialSpawn_distinct)
{
	LUA( "InitialFired, SpawnFired = 0, 0\n"
		"GAMEMODE = {\n"
		"	PlayerInitialSpawn = function() InitialFired = InitialFired + 1 end,\n"
		"	PlayerSpawn = function() SpawnFired = SpawnFired + 1 end }\n"
		"runinitial()" );
	CHECK_LUA_INT( "InitialFired", 1, "PlayerInitialSpawn: fired once" );
	CHECK_LUA_INT( "SpawnFired", 0, "PlayerInitialSpawn: did not fire PlayerSpawn" );
	LUA( "runspawn()" );
	CHECK_LUA_INT( "SpawnFired", 1, "PlayerSpawn: fired independently" );
	CHECK_LUA_INT( "InitialFired", 1, "PlayerSpawn: did not fire PlayerInitialSpawn" );
}

// Pins: PlayerDisconnected delivers the player through hook and GM.
TEST(Disconnected_identity)
{
	LUA( "hook.Add('PlayerDisconnected', 'rec', function(ply) DiscPly = ply end); rundisconnected()" );
	CHECK_LUA_BOOL( "rawequal(DiscPly, PlayerFixture)", true, "PlayerDisconnected: hook receives the player" );
	LUA( "hook.Remove('PlayerDisconnected', 'rec')\n"
		"GAMEMODE = { PlayerDisconnected = function(gm, ply) DiscGM = ply end }; rundisconnected()" );
	CHECK_LUA_BOOL( "rawequal(DiscGM, PlayerFixture)", true, "PlayerDisconnected: GM fallback receives the player" );
}

// Pins: PlayerDeath argument order and identity (victim, inflictor, attacker).
TEST(Death_args_order)
{
	LUA( "hook.Add('PlayerDeath', 'rec', function(v, i, a) Seen = { v, i, a } end); rundeath()" );
	CHECK_LUA_BOOL( "rawequal(Seen[1], VictimFixture)", true, "PlayerDeath: arg1 is victim" );
	CHECK_LUA_BOOL( "rawequal(Seen[2], InflictorFixture)", true, "PlayerDeath: arg2 is inflictor" );
	CHECK_LUA_BOOL( "rawequal(Seen[3], AttackerFixture)", true, "PlayerDeath: arg3 is attacker" );
	LUA( "hook.Remove('PlayerDeath', 'rec')\n"
		"GAMEMODE = { PlayerDeath = function(gm, v, i, a) GMSeen = { rawequal(gm, GAMEMODE), v, i, a } end }\n"
		"rundeath()" );
	CHECK_LUA_BOOL( "GMSeen[1]", true, "PlayerDeath: GM called with itself" );
	CHECK_LUA_BOOL( "rawequal(GMSeen[2], VictimFixture) and rawequal(GMSeen[3], InflictorFixture) and rawequal(GMSeen[4], AttackerFixture)", true, "PlayerDeath: GM receives all three entities" );
}

// Pins: NULL inflictor/attacker map to nil (FNullEnt path).
TEST(Death_nil_args)
{
	LUA( "GAMEMODE = { PlayerDeath = function(gm, v, i, a) NilMarks = { v ~= nil, i == nil, a == nil } end }\n"
		"deathargs(false, false); rundeath()" );
	CHECK_LUA_BOOL( "NilMarks[1]", true, "PlayerDeath: victim always present" );
	CHECK_LUA_BOOL( "NilMarks[2]", true, "PlayerDeath: null inflictor -> nil" );
	CHECK_LUA_BOOL( "NilMarks[3]", true, "PlayerDeath: null attacker -> nil" );
	LUA( "deathargs(true, false); rundeath()" );
	CHECK_LUA_BOOL( "NilMarks[2]", false, "PlayerDeath: attacker-only nil path keeps inflictor" );
	CHECK_LUA_BOOL( "NilMarks[3]", true, "PlayerDeath: attacker-only nil path" );
	LUA( "deathargs(true, true)" );
}

// Pins: a raising GM is reported once with the hook.Run context; the stack
// survives; hook-internal errors are contained by hook.lua.
TEST(Protected_error_containment)
{
	LUA( "GAMEMODE = { PlayerSpawn = function() error('expected gm spawn error') end }; runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: raising GM reported once" );
	CHECK_LUA_BOOL( "select(2, errors()):find('expected gm spawn error', 1, true) ~= nil", true, "HookRunProtected: GM error message preserved" );
	CHECK_LUA_STR( "select(3, errors())", "hook.Run(\"PlayerSpawn\")", "HookRunProtected: context names the event" );
	LUA( "GAMEMODE = nil; Ran = false\n"
		"hook.Add('PlayerSpawn', 'bad', function() error('hook spawn error') end)\n"
		"hook.Add('PlayerSpawn', 'good', function() Ran = true end)\n"
		"runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: hook error contained by hook.lua (no new report)" );
	CHECK_LUA_BOOL( "Ran", true, "hook.lua: chain continued after the erroring hook" );
	LUA( "hook.Remove('PlayerSpawn', 'bad'); hook.Remove('PlayerSpawn', 'good')" );
}

// Pins: a throwing argument pusher is inside the protected region.
TEST(Protected_pusher_error)
{
	LUA( "armpusher(true); runspawn(); armpusher(false)" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: pusher error reported" );
	CHECK_LUA_BOOL( "select(2, errors()):find('pusher boom', 1, true) ~= nil", true, "HookRunProtected: pusher error message" );
	LUA( "runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: dispatch healthy after pusher failure" );
}

// Pins: hostile hook globals — missing/non-table/non-function Run stay
// silent; a throwing _G.__index on the hook lookup is reported.
TEST(Protected_hostile_globals)
{
	LUA( "SavedHook = hook; hook = nil; runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 0, "HookRunProtected: missing hook global silent" );
	LUA( "hook = { Run = 42 }; runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 0, "HookRunProtected: non-function Run silent" );
	LUA( "hook = 42; runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 0, "HookRunProtected: non-table hook silent" );
	LUA( "hook = nil" );
	// Install a throwing __index via a metatable on _G (raw API, no lookups).
	lua_pushvalue( L, LUA_GLOBALSINDEX );
	lua_newtable( L );
	lua_pushliteral( L, "__index" );
	lua_pushcfunction( L, fx_throwingindex );
	lua_rawset( L, -3 );
	lua_setmetatable( L, -2 );
	lua_pop( L, 1 );
	XGLua_HookRunProtected( L, "PlayerSpawn", PushPlayer, NULL, onError );
	CHECK_EQ_INT( reportedErrors, 1, "HookRunProtected: throwing _G.__index reported" );
	CHECK( strstr( lastMessage, "global lookup boom" ) != NULL, "HookRunProtected: _G.__index error message" );
	lua_pushvalue( L, LUA_GLOBALSINDEX );
	lua_pushnil( L );
	lua_setmetatable( L, -2 );
	lua_pop( L, 1 );
	LUA( "hook = SavedHook; runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: recovered after hostile _G" );
}

// Pins: a throwing __index on the hook table (Run lookup) is reported.
TEST(Protected_hook_table_metamethod)
{
	LUA( "hook = setmetatable({}, { __index = function() error('run lookup boom') end }); runspawn()" );
	CHECK_LUA_INT( "select(1, errors())", 1, "HookRunProtected: hook table __index error reported" );
	CHECK_LUA_BOOL( "select(2, errors()):find('run lookup boom', 1, true) ~= nil", true, "HookRunProtected: hook table error message" );
	LUA( "hook = SavedHook" );
}

// Pins: NULL state/event guards are silent no-ops.
TEST(Protected_null_guards)
{
	XGLua_HookRunProtected( NULL, "PlayerSpawn", PushPlayer, NULL, onError );
	XGLua_HookRunProtected( L, NULL, PushPlayer, NULL, onError );
	CHECK_EQ_INT( reportedErrors, 0, "HookRunProtected: NULL guards silent" );
	CHECK_LUA_BOOL( "true", true, "HookRunProtected: NULL guards did not crash" );
}

// Pins: repeated dispatches fire exactly once each with stable identity.
TEST(Protected_repeat_stability)
{
	LUA( "GAMEMODE = {\n"
		"	PlayerSpawn = function(gm, ply) S = (S or 0) + 1; LastS = ply end,\n"
		"	PlayerInitialSpawn = function() I = (I or 0) + 1 end,\n"
		"	PlayerDisconnected = function() D = (D or 0) + 1 end,\n"
		"	PlayerDeath = function(gm, v) K = (K or 0) + 1; LastV = v end }\n"
		"for i = 1, 1000 do runspawn(); runinitial(); rundisconnected(); rundeath() end" );
	CHECK_LUA_INT( "S", 1000, "PlayerSpawn: one dispatch per call" );
	CHECK_LUA_INT( "I", 1000, "PlayerInitialSpawn: one dispatch per call" );
	CHECK_LUA_INT( "D", 1000, "PlayerDisconnected: one dispatch per call" );
	CHECK_LUA_INT( "K", 1000, "PlayerDeath: one dispatch per call" );
	CHECK_LUA_BOOL( "rawequal(LastS, PlayerFixture) and rawequal(LastV, VictimFixture)", true, "dispatch: identity stable across 1000 calls" );
	CHECK_LUA_INT( "select(1, errors())", 0, "dispatch: no errors across 1000 calls" );
}

// Pins: hook.lua removes a hook after 10 consecutive errors; re-Add resets.
TEST(HookLib_error_auto_remove)
{
	LUA( "hook.Add('Evil', 'bad', function() error('repeat failure') end)\n"
		"for i = 1, 9 do hook.Run('Evil') end" );
	CHECK_LUA_BOOL( "hook.GetTable().Evil.bad ~= nil", true, "hook.lua: still registered after 9 errors" );
	LUA( "hook.Run('Evil')" );
	CHECK_LUA_NIL( "hook.GetTable().Evil.bad", "hook.lua: removed after 10 consecutive errors" );
	CHECK_LUA_BOOL( "PrintLog[#PrintLog]:find('removing it', 1, true) ~= nil", true, "hook.lua: removal announced" );
	LUA( "hook.Add('Evil', 'bad', function() error('again') end); hook.Run('Evil')" );
	CHECK_LUA_BOOL( "hook.GetTable().Evil.bad ~= nil", true, "hook.lua: re-Add resets the error counter" );
	LUA( "hook.Remove('Evil', 'bad')" );
}

// Pins: a successful call resets the consecutive-error counter.
TEST(HookLib_error_counter_reset)
{
	LUA( "Toggle = false\n"
		"hook.Add('Flaky', 'f', function() if Toggle then error('flaky') end end)\n"
		"Toggle = true\n"
		"for i = 1, 9 do hook.Run('Flaky') end\n"
		"Toggle = false; hook.Run('Flaky')\n"
		"Toggle = true\n"
		"for i = 1, 9 do hook.Run('Flaky') end" );
	CHECK_LUA_BOOL( "hook.GetTable().Flaky.f ~= nil", true, "hook.lua: success between errors resets the counter" );
	LUA( "hook.Run('Flaky')" );
	CHECK_LUA_NIL( "hook.GetTable().Flaky.f", "hook.lua: removed on the 10th consecutive error after reset" );
}

// Pins: the callback list is snapshotted — mid-call add/remove is safe.
TEST(HookLib_midcall_snapshot)
{
	LUA( "ARan, BRan = 0, 0\n"
		"hook.Add('Snap', 'A', function()\n"
		"	ARan = ARan + 1\n"
		"	hook.Remove('Snap', 'A')\n"
		"	hook.Add('Snap', 'B', function() BRan = BRan + 1 end)\n"
		"end)\n"
		"hook.Run('Snap')" );
	CHECK_LUA_INT( "ARan", 1, "hook.lua: snapshot ran the removing hook once" );
	CHECK_LUA_INT( "BRan", 0, "hook.lua: hook added mid-call waits for the next dispatch" );
	LUA( "hook.Run('Snap')" );
	CHECK_LUA_INT( "ARan", 1, "hook.lua: self-removed hook does not run again" );
	CHECK_LUA_INT( "BRan", 1, "hook.lua: mid-call-added hook runs next dispatch" );
	LUA( "hook.Remove('Snap', 'B')" );
}

// Pins: integer identifiers work and never alias array positions.
TEST(HookLib_integer_identifiers)
{
	LUA( "hook.Add('IntIds', 1, function() One = true end)\n"
		"hook.Add('IntIds', 2, function() Two = true end)\n"
		"hook.Run('IntIds')" );
	CHECK_LUA_BOOL( "One", true, "hook.lua: integer identifier 1 dispatches" );
	CHECK_LUA_BOOL( "Two", true, "hook.lua: integer identifier 2 dispatches" );
	LUA( "hook.Remove('IntIds', 1); One = false; hook.Run('IntIds')" );
	CHECK_LUA_BOOL( "One", false, "hook.lua: integer identifier removal works" );
	LUA( "hook.Remove('IntIds', 2)" );
}

// Pins: hook.Call forwards all return values including trailing nils.
TEST(HookLib_multi_return)
{
	LUA( "hook.Add('Multi', 'm', function() return 1, nil, 3 end)\n"
		"A, B, C = hook.Call('Multi', nil)" );
	CHECK_LUA_INT( "A", 1, "hook.Call: first return forwarded" );
	CHECK_LUA_NIL( "B", "hook.Call: middle nil preserved" );
	CHECK_LUA_INT( "C", 3, "hook.Call: value after nil preserved" );
	LUA( "hook.Remove('Multi', 'm')\n"
		"GAMEMODE = { Multi = function(gm) return 'gm', nil end }\n"
		"D, E = hook.Run('Multi')" );
	CHECK_LUA_STR( "D", "gm", "hook.Call: GM fallback results forwarded" );
	CHECK_LUA_NIL( "E", "hook.Call: GM trailing nil preserved" );
	LUA( "GAMEMODE = nil" );
}

int main()
{
	xtest::Setup() = SetupPlayerHooks;
	return xtest::RunAll( "player_hooks" );
}