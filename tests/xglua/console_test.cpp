// Regression for dlls/lua/xglua_console.h: concommand.Add/Remove name
// validation and the 128-entry limit, lua_cmd dispatch semantics (nil player,
// name, argv table, token join), usage/unknown alerts, protected error
// reporting, and GetConVar allowlist/flag filtering, snapshot independence,
// numeric semantics, the 4096-byte cap and metatable protection.
// Engine boundary (ALERT/CMD_ARGC/CMD_ARGV/cvar lookup) is stubbed; the
// protected boundary is the real xglua_safe.h.

#include "test_framework.h"
#include "xglua_safe.h"

//---------------------------------------------------------------------------
// engine stubs
//---------------------------------------------------------------------------
static int alertCount;
static char lastAlertText[256];
#define ALERT( at, ... ) do { ++alertCount; std::snprintf( lastAlertText, sizeof( lastAlertText ), __VA_ARGS__ ); } while( 0 )

struct cvar_t { const char *string; float value; int flags; };
#define FCVAR_USERINFO (1<<1)
#define FCVAR_PROTECTED (1<<5)
#define FCVAR_PRIVILEGED (1<<10)
static cvar_t cvars[8];
static char cvarStrings[8][4400];
static const char *cvarNames[8];
static int cvarCount;
static cvar_t *fakeCvarGetPointer( const char *name )
{
	for( int i = 0; i < cvarCount; ++i )
		if( cvarNames[i] && !std::strcmp( cvarNames[i], name ) ) return &cvars[i];
	return NULL;
}

static char argBuf[68][64];
static const char *argvTokens[68];
static int argCount;
static int fakeArgc() { return argCount; }
static const char *fakeArgv( int i ) { return i >= 0 && i < argCount ? argvTokens[i] : NULL; }
struct EngFuncs
{
	cvar_t *(*pfnCVarGetPointer)( const char * );
	int (*pfnCmd_Argc)();
	const char *(*pfnCmd_Argv)( int );
};
static EngFuncs g_engfuncs;
#define CMD_ARGC ( *g_engfuncs.pfnCmd_Argc )
#define CMD_ARGV ( *g_engfuncs.pfnCmd_Argv )

static lua_State *g_xglua_L;
static int errorReports;
static char lastErrorText[512];
static char lastErrorContext[64];
static void XGLua_ReportError( lua_State *L, const char *context )
{
	++errorReports;
	const char *text = XGLua_ErrorText( L, -1 );
	std::snprintf( lastErrorText, sizeof( lastErrorText ), "%s", text ? text : "?" );
	std::snprintf( lastErrorContext, sizeof( lastErrorContext ), "%s", context ? context : "" );
	lua_pop( L, 1 );
}
#include "xglua_console.h"

//---------------------------------------------------------------------------
// fixture helpers
//---------------------------------------------------------------------------
static int fx_cmdline( lua_State *L )
{
	int n = lua_gettop( L );
	if( n > 67 ) return luaL_error( L, "fixture token limit" );
	argCount = n;
	for( int i = 0; i < n; ++i )
	{
		std::snprintf( argBuf[i], sizeof( argBuf[i] ), "%s", luaL_checkstring( L, i + 1 ) );
		argvTokens[i] = argBuf[i];
	}
	return 0;
}
static int fx_luacmd( lua_State * ) { XGLua_LuaCmd_f(); return 0; }
static int fx_alerts( lua_State *L ) { lua_pushinteger( L, alertCount ); lua_pushstring( L, lastAlertText ); return 2; }
static int fx_reports( lua_State *L ) { lua_pushinteger( L, errorReports ); lua_pushstring( L, lastErrorText ); lua_pushstring( L, lastErrorContext ); return 3; }
static int fx_cvarset( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	cvar_t *v = fakeCvarGetPointer( name );
	int slot;
	if( v ) slot = (int)( v - cvars );
	else
	{
		if( cvarCount >= 8 ) return luaL_error( L, "fixture cvar registry full" );
		slot = cvarCount++;
		cvarNames[slot] = name;
		v = &cvars[slot];
	}
	std::snprintf( cvarStrings[slot], sizeof( cvarStrings[slot] ), "%s", luaL_checkstring( L, 2 ) );
	v->string = cvarStrings[slot];
	v->value = (float)luaL_checknumber( L, 3 );
	v->flags = (int)luaL_checkinteger( L, 4 );
	return 0;
}
static int fx_cvarmutate( lua_State *L )
{
	cvar_t *v = fakeCvarGetPointer( luaL_checkstring( L, 1 ) );
	if( !v ) return luaL_error( L, "no such fixture cvar" );
	int slot = (int)( v - cvars );
	std::snprintf( cvarStrings[slot], sizeof( cvarStrings[slot] ), "%s", luaL_checkstring( L, 2 ) );
	v->value = (float)luaL_checknumber( L, 3 );
	return 0;
}
static int fx_cvarengine( lua_State *L ) { g_engfuncs.pfnCVarGetPointer = lua_toboolean( L, 1 ) ? fakeCvarGetPointer : NULL; return 0; }
static int fx_setvm( lua_State *L ) { g_xglua_L = lua_toboolean( L, 1 ) ? L : NULL; return 0; }

static bool SetupConsole( lua_State *L )
{
	alertCount = 0; lastAlertText[0] = '\0';
	errorReports = 0; lastErrorText[0] = '\0'; lastErrorContext[0] = '\0';
	cvarCount = 0; argCount = 0;
	g_xglua_L = L;
	g_engfuncs.pfnCVarGetPointer = fakeCvarGetPointer;
	g_engfuncs.pfnCmd_Argc = fakeArgc;
	g_engfuncs.pfnCmd_Argv = fakeArgv;
	XGLua_RegisterConsole( L );
	static const luaL_Reg helpers[] = {
		{ "cmdline", fx_cmdline }, { "luacmd", fx_luacmd }, { "alerts", fx_alerts },
		{ "reports", fx_reports }, { "cvarset", fx_cvarset }, { "cvarmutate", fx_cvarmutate },
		{ "cvarengine", fx_cvarengine }, { "setvm", fx_setvm }, { NULL, NULL } };
	for( const luaL_Reg *f = helpers; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setglobal( L, f->name ); }
	return true;
}

// Pins: the registration surface exists after XGLua_RegisterConsole.
TEST(Console_register_surface)
{
	CHECK_LUA_TYPE( "concommand", "table", "concommand: library table" );
	CHECK_LUA_TYPE( "concommand.Add", "function", "concommand.Add: registered" );
	CHECK_LUA_TYPE( "concommand.Remove", "function", "concommand.Remove: registered" );
	CHECK_LUA_TYPE( "GetConVar", "function", "GetConVar: global registered" );
}

// Pins: valid names (lowercase/digits/underscore, 1..63) register and the
// callback is dispatchable.
TEST(Console_add_valid_names)
{
	LUA( "concommand.Add('goodcmd', function() Hit = true end)\n"
		"concommand.Add(('a'):rep(63), function() end)\n"
		"concommand.Add('cmd_2', function() end)\n"
		"cmdline('lua_cmd', 'goodcmd'); luacmd()" );
	CHECK_LUA_BOOL( "Hit", true, "concommand.Add: registered command dispatches" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "concommand.Add: valid names produce no alerts" );
}

// Pins: invalid names are rejected with the charset argerror.
TEST(Console_add_invalid_names)
{
	CHECK_LUA_ERROR( "concommand.Add('', function() end)", "expected 1-63 lowercase letters, digits or underscores", "concommand.Add: empty name rejected" );
	CHECK_LUA_ERROR( "concommand.Add('UPPER', function() end)", "expected 1-63 lowercase", "concommand.Add: uppercase rejected" );
	CHECK_LUA_ERROR( "concommand.Add('with-dash', function() end)", "expected 1-63 lowercase", "concommand.Add: dash rejected" );
	CHECK_LUA_ERROR( "concommand.Add('with space', function() end)", "expected 1-63 lowercase", "concommand.Add: space rejected" );
	CHECK_LUA_ERROR( "concommand.Add(('a'):rep(64), function() end)", "expected 1-63 lowercase", "concommand.Add: 64 chars rejected" );
	CHECK_LUA_ERROR( "concommand.Add('a\\0b', function() end)", "expected 1-63 lowercase", "concommand.Add: NUL rejected" );
	CHECK_LUA_ERROR( "concommand.Add(123, function() end)", "string expected", "concommand.Add: number name rejected" );
}

// Pins: the callback must be a function.
TEST(Console_add_callback_validation)
{
	CHECK_LUA_ERROR( "concommand.Add('okname', nil)", "function expected", "concommand.Add: nil callback rejected" );
	CHECK_LUA_ERROR( "concommand.Add('okname')", "function expected", "concommand.Add: missing callback rejected" );
	CHECK_LUA_ERROR( "concommand.Add('okname', 42)", "function expected", "concommand.Add: number callback rejected" );
}

// Pins: re-adding a name replaces the callback.
TEST(Console_add_replace)
{
	LUA( "concommand.Add('dup', function() Which = 'first' end)\n"
		"concommand.Add('dup', function() Which = 'second' end)\n"
		"cmdline('lua_cmd', 'dup'); luacmd()" );
	CHECK_LUA_STR( "Which", "second", "concommand.Add: re-add replaces the callback" );
}

// Pins: Remove unregisters; re-add works; names stay validated.
TEST(Console_remove_cycle)
{
	LUA( "concommand.Add('cycle', function() CycleHit = true end)\n"
		"concommand.Remove('cycle')\n"
		"cmdline('lua_cmd', 'cycle'); luacmd()" );
	CHECK_LUA_NIL( "CycleHit", "concommand.Remove: removed command does not dispatch" );
	CHECK_LUA_BOOL( "select(2, alerts()):find('unknown Lua command', 1, true) ~= nil", true, "concommand.Remove: dispatch alerts unknown" );
	LUA( "concommand.Add('cycle', function() CycleHit = true end)\n"
		"cmdline('lua_cmd', 'cycle'); luacmd()" );
	CHECK_LUA_BOOL( "CycleHit", true, "concommand.Remove: re-added command dispatches" );
	CHECK_LUA_ERROR( "concommand.Remove()", "string expected", "concommand.Remove: name still validated" );
	CHECK_LUA_ERROR( "concommand.Remove('Bad-Name')", "expected 1-63 lowercase", "concommand.Remove: charset enforced" );
}

// Pins: the 128-entry cap; re-add bypasses it; Remove frees a slot.
TEST(Console_limit_128)
{
	LUA( "for i = 1, 128 do concommand.Add('c' .. i, function() end) end" );
	CHECK_LUA_ERROR( "concommand.Add('overflow', function() end)", "Lua command limit reached (128)", "concommand.Add: 129th command rejected" );
	LUA( "concommand.Add('c7', function() end)" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "concommand.Add: re-add at the limit is allowed" );
	LUA( "concommand.Remove('c7'); concommand.Add('overflow', function() end)" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "concommand.Add: freed slot reusable" );
}

// Pins: dispatch passes (nil, name, 1-based argv table, space-joined raw).
TEST(Console_dispatch_args)
{
	LUA( "concommand.Add('sayhi', function(ply, cmd, args, raw) Rec = { ply, cmd, args, raw } end)\n"
		"cmdline('lua_cmd', 'sayhi', 'hello', 'world'); luacmd()" );
	CHECK_LUA_NIL( "Rec[1]", "lua_cmd: no player object (nil first arg)" );
	CHECK_LUA_STR( "Rec[2]", "sayhi", "lua_cmd: command name echoed" );
	CHECK_LUA_INT( "#Rec[3]", 2, "lua_cmd: argv table size" );
	CHECK_LUA_STR( "Rec[3][1]", "hello", "lua_cmd: argv 1-based first token" );
	CHECK_LUA_STR( "Rec[3][2]", "world", "lua_cmd: argv second token" );
	CHECK_LUA_STR( "Rec[4]", "hello world", "lua_cmd: raw string joins tokens with spaces" );
}

// Pins: zero-argument dispatch gives an empty table and empty raw string.
TEST(Console_dispatch_noargs)
{
	LUA( "concommand.Add('bare', function(ply, cmd, args, raw) Rec2 = { args, raw } end)\n"
		"cmdline('lua_cmd', 'bare'); luacmd()" );
	CHECK_LUA_INT( "#Rec2[1]", 0, "lua_cmd: no args -> empty table" );
	CHECK_LUA_STR( "Rec2[2]", "", "lua_cmd: no args -> empty raw string" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "lua_cmd: clean dispatch is silent" );
}

// Pins: unknown commands alert without a Lua error report.
TEST(Console_dispatch_unknown)
{
	LUA( "cmdline('lua_cmd', 'nosuchcmd'); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 1, "lua_cmd: unknown command alerts once" );
	CHECK_LUA_BOOL( "select(2, alerts()):find('nosuchcmd', 1, true) ~= nil", true, "lua_cmd: alert names the command" );
	CHECK_LUA_INT( "select(1, reports())", 0, "lua_cmd: unknown command is not a Lua error" );
}

// Pins: usage alerts for missing/invalid names and more than 64 args.
TEST(Console_usage_alerts)
{
	LUA( "cmdline('lua_cmd', 'BadName'); luacmd()" );
	CHECK_LUA_BOOL( "select(2, alerts()):find('usage', 1, true) ~= nil", true, "lua_cmd: invalid name -> usage" );
	LUA( "cmdline('lua_cmd'); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 2, "lua_cmd: missing name -> usage" );
	LUA( "cmdline(); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 3, "lua_cmd: zero tokens -> usage" );
	LUA( "concommand.Add('c3', function() end)\n"
		"local many = { 'lua_cmd', 'c3' }\n"
		"for i = 1, 65 do many[#many + 1] = 'a' .. i end\n"
		"cmdline(unpack(many)); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 4, "lua_cmd: 67 tokens -> usage" );
	CHECK_LUA_BOOL( "select(2, alerts()):find('usage', 1, true) ~= nil", true, "lua_cmd: over-limit alert is the usage text" );
	LUA( "local ok66 = { 'lua_cmd', 'c3' }\n"
		"for i = 1, 64 do ok66[#ok66 + 1] = 'a' .. i end\n"
		"cmdline(unpack(ok66)); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 4, "lua_cmd: 66 tokens (64 args) accepted silently" );
}

// Pins: handler errors are reported once with context lua_cmd; VM survives.
TEST(Console_error_contained)
{
	LUA( "concommand.Add('boom', function() error('kapow', 0) end)\n"
		"cmdline('lua_cmd', 'boom'); luacmd()" );
	CHECK_LUA_INT( "select(1, reports())", 1, "lua_cmd: handler error reported once" );
	CHECK_LUA_STR( "select(2, reports())", "kapow", "lua_cmd: error text preserved" );
	CHECK_LUA_STR( "select(3, reports())", "lua_cmd", "lua_cmd: error context" );
	LUA( "concommand.Add('afterboom', function() end)" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "lua_cmd: console usable after a failed dispatch" );
}

// Pins: with no VM the command is a silent no-op.
TEST(Console_no_vm)
{
	LUA( "setvm(false); cmdline('lua_cmd', 'whatever'); luacmd()" );
	CHECK_LUA_INT( "select(1, alerts())", 0, "lua_cmd: no VM -> silent return" );
	CHECK_LUA_INT( "select(1, reports())", 0, "lua_cmd: no VM -> no error report" );
	LUA( "setvm(true)" );
}

// Pins: GetConVar snapshot accessors on allowlisted variables.
TEST(Convar_basics)
{
	CHECK_LUA_NIL( "GetConVar('deathmatch')", "GetConVar: unregistered variable nil" );
	LUA( "cvarset('deathmatch', '1', 1, 0); V = GetConVar('deathmatch')" );
	CHECK_LUA_TYPE( "V", "userdata", "GetConVar: snapshot userdata" );
	CHECK_LUA_STR( "V:GetName()", "deathmatch", "ConVar:GetName" );
	CHECK_LUA_STR( "V:GetString()", "1", "ConVar:GetString" );
	CHECK_LUA_FLT( "V:GetFloat()", 1, "ConVar:GetFloat" );
	CHECK_LUA_INT( "V:GetInt()", 1, "ConVar:GetInt" );
	CHECK_LUA_BOOL( "V:GetBool()", true, "ConVar:GetBool nonzero" );
}

// Pins: GetInt truncates toward zero; GetBool is nonzero; zero is false.
TEST(Convar_numeric_semantics)
{
	LUA( "cvarset('sv_gravity', '800.5', 800.5, 0)" );
	CHECK_LUA_FLT( "GetConVar('sv_gravity'):GetFloat()", 800.5, "ConVar:GetFloat fractional" );
	CHECK_LUA_INT( "GetConVar('sv_gravity'):GetInt()", 800, "ConVar:GetInt truncates positive" );
	LUA( "cvarset('skill', '-3.2', -3.2, 0)" );
	CHECK_LUA_INT( "GetConVar('skill'):GetInt()", -3, "ConVar:GetInt truncates toward zero" );
	LUA( "cvarset('sv_maxspeed', '0', 0, 0)" );
	CHECK_LUA_BOOL( "GetConVar('sv_maxspeed'):GetBool()", false, "ConVar:GetBool zero is false" );
	CHECK_LUA_INT( "GetConVar('sv_maxspeed'):GetInt()", 0, "ConVar:GetInt zero" );
}

// Pins: allowlist and engine-callback gating.
TEST(Convar_allowlist)
{
	LUA( "cvarset('mp_teamplay', '1', 1, 0)" );
	CHECK_LUA_NIL( "GetConVar('mp_teamplay')", "GetConVar: registered but not allowlisted -> nil" );
	LUA( "cvarset('deathmatch', '1', 1, 0); cvarengine(false)" );
	CHECK_LUA_NIL( "GetConVar('deathmatch')", "GetConVar: engine lookup unavailable -> nil" );
	LUA( "cvarengine(true)" );
	CHECK_LUA_TYPE( "GetConVar('deathmatch')", "userdata", "GetConVar: works again with the engine back" );
	CHECK_LUA_NIL( "GetConVar('xgl_gamemode')", "GetConVar: allowlisted but unregistered -> nil" );
}

// Pins: PROTECTED/PRIVILEGED/USERINFO flags hide the variable.
TEST(Convar_flag_filter)
{
	LUA( "cvarset('deathmatch', '1', 1, 32)" );
	CHECK_LUA_NIL( "GetConVar('deathmatch')", "GetConVar: FCVAR_PROTECTED hidden" );
	LUA( "cvarset('deathmatch', '1', 1, 1024)" );
	CHECK_LUA_NIL( "GetConVar('deathmatch')", "GetConVar: FCVAR_PRIVILEGED hidden" );
	LUA( "cvarset('deathmatch', '1', 1, 2)" );
	CHECK_LUA_NIL( "GetConVar('deathmatch')", "GetConVar: FCVAR_USERINFO hidden" );
	LUA( "cvarset('deathmatch', '1', 1, 0)" );
	CHECK_LUA_TYPE( "GetConVar('deathmatch')", "userdata", "GetConVar: unflagged readable" );
}

// Pins: snapshots are detached from engine storage; fresh calls see updates;
// non-finite engine values degrade to 0.
TEST(Convar_snapshot_independence)
{
	LUA( "cvarset('deathmatch', '1', 1, 0); Snap = GetConVar('deathmatch')\n"
		"cvarmutate('deathmatch', '999', 999)" );
	CHECK_LUA_STR( "Snap:GetString()", "1", "ConVar snapshot: string independent of engine mutation" );
	CHECK_LUA_FLT( "Snap:GetFloat()", 1, "ConVar snapshot: value independent of engine mutation" );
	LUA( "Fresh = GetConVar('deathmatch')" );
	CHECK_LUA_BOOL( "Fresh == Snap", false, "ConVar snapshot: each call is a new object" );
	CHECK_LUA_STR( "Fresh:GetString()", "999", "ConVar snapshot: fresh call sees the update" );
	LUA( "cvarset('developer', 'x', 0/0, 0)" );
	CHECK_LUA_FLT( "GetConVar('developer'):GetFloat()", 0, "ConVar: non-finite value degrades to 0" );
	CHECK_LUA_BOOL( "GetConVar('developer'):GetBool()", false, "ConVar: degraded value is falsy" );
}

// Pins: the 4096-byte value cap (4096 accepted, 4097 raises).
TEST(Convar_value_limit)
{
	LUA( "cvarset('developer', ('x'):rep(4097), 1, 0)" );
	CHECK_LUA_ERROR( "return GetConVar('developer')", "ConVar value exceeds 4096 bytes", "GetConVar: 4097-byte value rejected" );
	LUA( "cvarset('developer', ('x'):rep(4096), 1, 0)" );
	CHECK_LUA_INT( "#GetConVar('developer'):GetString()", 4096, "GetConVar: 4096-byte value accepted" );
}

// Pins: metatable protection and wrong-self rejection. Note: Lua 5.1's
// debug.setmetatable bypasses __metatable by design; the runtime sandbox
// removes debug, so production relies on that rather than on the tag alone.
TEST(Convar_metatable_protection)
{
	LUA( "cvarset('deathmatch', '1', 1, 0); V = GetConVar('deathmatch')" );
	CHECK_LUA_STR( "getmetatable(V)", "ConVar", "ConVar: __metatable tag hides the real table" );
	CHECK_LUA_ERROR( "setmetatable(V, {})", "table expected, got userdata", "ConVar: setmetatable rejects userdata outright" );
	LUA( "Mt = debug.getmetatable(V)" );
	CHECK_LUA_TYPE( "Mt.__index.GetName", "function", "ConVar: methods live on __index" );
	CHECK_LUA_ERROR( "return Mt.__index.GetName({})", "XGLua_ConVar expected", "ConVar: wrong self type rejected" );
	CHECK_LUA_ERROR( "return V.GetName(42)", "XGLua_ConVar expected", "ConVar: number self rejected" );
	LUA( "W = GetConVar('deathmatch'); debug.setmetatable(W, {})" );
	CHECK_LUA_NIL( "debug.getmetatable(W).__index", "ConVar: debug.setmetatable bypasses the tag (Lua 5.1 semantics)" );
}

// Pins: GetConVar validates names like concommand.
TEST(Convar_name_validation)
{
	CHECK_LUA_ERROR( "return GetConVar('Upper')", "expected 1-63 lowercase", "GetConVar: uppercase rejected" );
	CHECK_LUA_ERROR( "return GetConVar('has-dash')", "expected 1-63 lowercase", "GetConVar: dash rejected" );
	CHECK_LUA_ERROR( "return GetConVar('')", "expected 1-63 lowercase", "GetConVar: empty rejected" );
	CHECK_LUA_ERROR( "return GetConVar(123)", "string expected", "GetConVar: number rejected" );
}

int main()
{
	xtest::Setup() = SetupConsole;
	return xtest::RunAll( "console" );
}