// Regression for the NEW damage/use event system (dlls/lua/xglua_damage.h):
// PlayerUse / PlayerShouldTakeDamage deny-style vetoes, EntityTakeDamage
// cancel/modify dispatch and the DamageInfo userdata lifetime guard.
// Includes the REAL production header against stubbed game boundaries
// (g_xglua_L, XGLua_PushEntity, XGLua_ReportError, XGLua_DamageEntityValid)
// and loads the REAL lua/includes/modules/hook.lua, so hook priority, the
// GAMEMODE fallback and error containment are the shipped behaviors.
// Run from tests/xglua so hook.lua's relative path resolves.
//
// The public wrappers in xglua_damage.cpp (FNullEnt mapping, post-dispatch
// entity-validity cancel) and the TakeDamage/PlayerUse call sites are
// reviewed statically from the diff; this file is a helper-level test.

#include "test_framework.h"
#include "xglua_safe.h"

//---------------------------------------------------------------------------
// game-boundary stubs (before the production header)
//---------------------------------------------------------------------------
struct edict_s { bool free; int serialnumber; int flags; };
typedef struct edict_s edict_t;
#define FL_KILLME (1<<30)

// Mirror of XGLua_DamageEntityValid in dlls/lua/xglua_damage.cpp.
bool XGLua_DamageEntityValid( edict_t *pent, int serial )
{
	return pent && !pent->free && pent->serialnumber == serial && !( pent->flags & FL_KILLME );
}

lua_State *g_xglua_L = NULL;

static int reportedErrors;
static char lastMessage[256];
void XGLua_ReportError( lua_State *L, const char *context )
{
	++reportedErrors;
	const char *message = XGLua_ErrorText( L, -1 );
	snprintf( lastMessage, sizeof( lastMessage ), "%s", message ? message : "?" );
	(void)context;
	lua_pop( L, 1 );
}

static edict_t g_edicts[4];
static int edictRefs[4];
void XGLua_PushEntity( lua_State *L, edict_t *pent )
{
	if( !pent ) { lua_pushnil( L ); return; }
	lua_rawgeti( L, LUA_REGISTRYINDEX, edictRefs[pent - g_edicts] );
}

#include "xglua_damage.h"

//---------------------------------------------------------------------------
// fixture helpers
//---------------------------------------------------------------------------
static edict_t *ED( int slot ) { return slot >= 0 && slot < 4 ? &g_edicts[slot] : NULL; }

// Runs one EntityTakeDamage dispatch; results land in Lua globals.
static int fx_etd( lua_State *L )
{
	float dmg = (float)luaL_checknumber( L, 4 );
	int type = (int)luaL_checkinteger( L, 5 );
	XGLuaDmgCtx ctx = { ED( (int)luaL_checkinteger( L, 1 ) ), ED( (int)luaL_checkinteger( L, 2 ) ),
		ED( (int)luaL_checkinteger( L, 3 ) ), &dmg, &type, 0, 0, 0 };
	int cancelled = XGLua_EntityTakeDamageImpl( L, &ctx );
	lua_pushinteger( L, cancelled );
	lua_pushnumber( L, dmg );
	lua_pushinteger( L, type );
	return 3;
}
static int fx_use( lua_State *L )
{
	bool denied = XGLua_PlayerUseImpl( L, ED( (int)luaL_checkinteger( L, 1 ) ), ED( (int)luaL_checkinteger( L, 2 ) ) );
	lua_pushboolean( L, denied );
	return 1;
}
static int fx_should( lua_State *L )
{
	bool denied = XGLua_PlayerShouldTakeDamageImpl( L, ED( (int)luaL_checkinteger( L, 1 ) ), ED( (int)luaL_checkinteger( L, 2 ) ) );
	lua_pushboolean( L, denied );
	return 1;
}
// Nested dispatch from inside a hook: exercises live-slot save/restore.
static int fx_nest( lua_State *L )
{
	float dmg = 7.0f; int type = 1;
	XGLuaDmgCtx ctx = { &g_edicts[1], NULL, NULL, &dmg, &type, 0, 0, 0 };
	int cancelled = XGLua_EntityTakeDamageImpl( L, &ctx );
	lua_pushinteger( L, cancelled );
	lua_pushnumber( L, dmg );
	return 2;
}
static int fx_freeent( lua_State *L ) { g_edicts[(int)luaL_checkinteger( L, 1 )].free = true; return 0; }
static int fx_bumpserial( lua_State *L ) { ++g_edicts[(int)luaL_checkinteger( L, 1 )].serialnumber; return 0; }
static int fx_killme( lua_State *L ) { g_edicts[(int)luaL_checkinteger( L, 1 )].flags |= FL_KILLME; return 0; }
static int fx_resetent( lua_State *L )
{
	edict_t *e = &g_edicts[(int)luaL_checkinteger( L, 1 )];
	e->free = false; e->serialnumber = 0; e->flags = 0;
	return 0;
}
static int fx_errors( lua_State *L ) { lua_pushinteger( L, reportedErrors ); lua_pushstring( L, lastMessage ); return 2; }

static bool SetupDamage( lua_State *L )
{
	reportedErrors = 0; lastMessage[0] = '\0';
	memset( g_edicts, 0, sizeof( g_edicts ) );
	if( luaL_loadfile( L, "../../lua/includes/modules/hook.lua" ) || lua_pcall( L, 0, 0, 0 ) )
	{
		std::printf( "  fixture: hook.lua load failed: %s\n", lua_tostring( L, -1 ) );
		lua_pop( L, 1 );
		return false;
	}
	static const char *tags[4] = { "player", "target", "attacker", "inflictor" };
	static const char *globalNames[4] = { "PlayerFixture", "TargetFixture", "AttackerFixture", "InflictorFixture" };
	for( int i = 0; i < 4; ++i )
	{
		char code[64];
		snprintf( code, sizeof( code ), "return { tag = '%s' }", tags[i] );
		if( luaL_dostring( L, code ) ) return false;
		edictRefs[i] = luaL_ref( L, LUA_REGISTRYINDEX );
		lua_rawgeti( L, LUA_REGISTRYINDEX, edictRefs[i] );
		lua_setglobal( L, globalNames[i] );
	}
	g_xglua_L = L;
	static const luaL_Reg helpers[] = {
		{ "etd", fx_etd }, { "use", fx_use }, { "should", fx_should }, { "nest", fx_nest },
		{ "freeent", fx_freeent }, { "bumpserial", fx_bumpserial }, { "killme", fx_killme },
		{ "resetent", fx_resetent }, { "errors", fx_errors }, { NULL, NULL } };
	for( const luaL_Reg *f = helpers; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setglobal( L, f->name ); }
	return true;
}

//---------------------------------------------------------------------------
// PlayerUse
//---------------------------------------------------------------------------

// Pins: no handlers means the interaction is allowed.
TEST(Use_no_handlers_allow)
{
	CHECK_LUA_BOOL( "use(0, 1)", false, "PlayerUse: no handlers -> not denied" );
	CHECK_LUA_INT( "select(1, errors())", 0, "PlayerUse: silent without handlers" );
}

// Pins: arguments are (ply, target) with exact identity.
TEST(Use_args_identity)
{
	LUA( "hook.Add('PlayerUse', 'rec', function(ply, ent) Seen = { ply, ent } end); use(0, 1)" );
	CHECK_LUA_BOOL( "rawequal(Seen[1], PlayerFixture)", true, "PlayerUse: arg1 is the player" );
	CHECK_LUA_BOOL( "rawequal(Seen[2], TargetFixture)", true, "PlayerUse: arg2 is the target" );
}

// Pins: only a strict Lua false denies; every other result allows.
TEST(Use_strict_false_matrix)
{
	static const char *rets[] = { "false", "true", "nil", "0", "'x'", "{}", NULL };
	static const bool deny[] = { true, false, false, false, false, false };
	for( int i = 0; rets[i]; ++i )
	{
		char code[160], desc[96];
		snprintf( code, sizeof( code ), "hook.Add('PlayerUse', 't', function() return %s end)", rets[i] );
		LUA( code );
		snprintf( desc, sizeof( desc ), "PlayerUse: return %s -> %s", rets[i], deny[i] ? "deny" : "allow" );
		CHECK_LUA_BOOL( "use(0, 1)", deny[i], desc );
		LUA( "hook.Remove('PlayerUse', 't')" );
	}
}

// Pins: GM fallback denies on false and is called with itself.
TEST(Use_gm_fallback)
{
	LUA( "GAMEMODE = { PlayerUse = function(gm, ply, ent) GMSelf = rawequal(gm, GAMEMODE); return false end }" );
	CHECK_LUA_BOOL( "use(0, 1)", true, "PlayerUse: GM false denies" );
	CHECK_LUA_BOOL( "GMSelf", true, "PlayerUse: GM called with itself" );
}

// Pins: a non-nil hook return terminates the chain before the GM fallback.
TEST(Use_hook_priority)
{
	LUA( "GAMEMODE = { PlayerUse = function() return false end }\n"
		"hook.Add('PlayerUse', 't', function() return true end)" );
	CHECK_LUA_BOOL( "use(0, 1)", false, "PlayerUse: hook true beats GM false" );
	LUA( "hook.Remove('PlayerUse', 't'); GAMEMODE = nil" );
}

// Pins: a raising GM is reported and the interaction stays allowed.
TEST(Use_gm_error_allow)
{
	LUA( "GAMEMODE = { PlayerUse = function() error('gm use error') end }" );
	CHECK_LUA_BOOL( "use(0, 1)", false, "PlayerUse: GM error allows" );
	CHECK_LUA_INT( "select(1, errors())", 1, "PlayerUse: GM error reported once" );
	CHECK_LUA_BOOL( "select(2, errors()):find('gm use error', 1, true) ~= nil", true, "PlayerUse: error message preserved" );
}

// Pins: NULL state/player/target guards return "not denied" without Lua.
TEST(Use_null_guards)
{
	CHECK_EQ_BOOL( XGLua_PlayerUseImpl( NULL, &g_edicts[0], &g_edicts[1] ), false, "PlayerUse: NULL state guard" );
	CHECK_EQ_BOOL( XGLua_PlayerUseImpl( L, NULL, &g_edicts[1] ), false, "PlayerUse: NULL player guard" );
	CHECK_EQ_BOOL( XGLua_PlayerUseImpl( L, &g_edicts[0], NULL ), false, "PlayerUse: NULL target guard" );
	CHECK_EQ_INT( reportedErrors, 0, "PlayerUse: guards stay silent" );
}

//---------------------------------------------------------------------------
// PlayerShouldTakeDamage
//---------------------------------------------------------------------------

// Pins: no handlers keeps the native damage decision.
TEST(Should_no_handlers_allow)
{
	CHECK_LUA_BOOL( "should(0, 2)", false, "PlayerShouldTakeDamage: no handlers -> not exempt" );
}

// Pins: attacker identity and NULL attacker -> nil.
TEST(Should_attacker_mapping)
{
	LUA( "hook.Add('PlayerShouldTakeDamage', 'rec', function(ply, atk) SeenAtk = atk end)\n"
		"should(0, 2)" );
	CHECK_LUA_BOOL( "rawequal(SeenAtk, AttackerFixture)", true, "PlayerShouldTakeDamage: attacker identity" );
	LUA( "should(0, -1)" );
	CHECK_LUA_NIL( "SeenAtk", "PlayerShouldTakeDamage: NULL attacker -> nil" );
}

// Pins: strict false exempts; hook false beats GM true.
TEST(Should_deny_matrix_priority)
{
	LUA( "GAMEMODE = { PlayerShouldTakeDamage = function() return false end }" );
	CHECK_LUA_BOOL( "should(0, -1)", true, "PlayerShouldTakeDamage: GM false exempts" );
	LUA( "GAMEMODE = { PlayerShouldTakeDamage = function() return true end }\n"
		"hook.Add('PlayerShouldTakeDamage', 't', function() return false end)" );
	CHECK_LUA_BOOL( "should(0, -1)", true, "PlayerShouldTakeDamage: hook false beats GM true" );
	LUA( "hook.Remove('PlayerShouldTakeDamage', 't')\n"
		"GAMEMODE = { PlayerShouldTakeDamage = function() return 0 end }" );
	CHECK_LUA_BOOL( "should(0, -1)", false, "PlayerShouldTakeDamage: non-boolean does not exempt" );
	LUA( "GAMEMODE = nil" );
}

// Pins: NULL state/player guards.
TEST(Should_null_guards)
{
	CHECK_EQ_BOOL( XGLua_PlayerShouldTakeDamageImpl( NULL, &g_edicts[0], NULL ), false, "PlayerShouldTakeDamage: NULL state guard" );
	CHECK_EQ_BOOL( XGLua_PlayerShouldTakeDamageImpl( L, NULL, NULL ), false, "PlayerShouldTakeDamage: NULL player guard" );
}

//---------------------------------------------------------------------------
// EntityTakeDamage
//---------------------------------------------------------------------------

// Pins: no handlers continues with the caller's values untouched.
TEST(ETD_no_handlers_continue)
{
	LUA( "Cancel, Dmg, Type = etd(1, 3, 2, 25, 2)" );
	CHECK_LUA_INT( "Cancel", 0, "EntityTakeDamage: no handlers continues" );
	CHECK_LUA_FLT( "Dmg", 25, "EntityTakeDamage: damage untouched" );
	CHECK_LUA_INT( "Type", 2, "EntityTakeDamage: type untouched" );
}

// Pins: SetDamage/SetDamageType write through to the caller's values.
TEST(ETD_modify_writethrough)
{
	LUA( "hook.Add('EntityTakeDamage', 'mod', function(ent, info)\n"
		"	info:SetDamage( info:GetDamage() * 2 )\n"
		"	info:SetDamageType( info:GetDamageType() + 16 )\n"
		"end)\n"
		"Cancel, Dmg, Type = etd(1, 3, 2, 25, 2)" );
	CHECK_LUA_INT( "Cancel", 0, "EntityTakeDamage: modifier hook continues" );
	CHECK_LUA_FLT( "Dmg", 50, "EntityTakeDamage: SetDamage wrote through" );
	CHECK_LUA_INT( "Type", 18, "EntityTakeDamage: SetDamageType wrote through" );
}

// Pins: (ent, dmginfo) identity and GetAttacker/GetInflictor/GetDamage.
TEST(ETD_args_identity)
{
	LUA( "hook.Add('EntityTakeDamage', 'rec', function(ent, info)\n"
		"	Seen = { ent, info:GetAttacker(), info:GetInflictor(), info:GetDamage(), info:GetDamageType() }\n"
		"end)\n"
		"etd(1, 3, 2, 12.5, 4)" );
	CHECK_LUA_BOOL( "rawequal(Seen[1], TargetFixture)", true, "EntityTakeDamage: arg1 is the victim" );
	CHECK_LUA_BOOL( "rawequal(Seen[2], AttackerFixture)", true, "DamageInfo:GetAttacker identity" );
	CHECK_LUA_BOOL( "rawequal(Seen[3], InflictorFixture)", true, "DamageInfo:GetInflictor identity" );
	CHECK_LUA_FLT( "Seen[4]", 12.5, "DamageInfo:GetDamage value" );
	CHECK_LUA_INT( "Seen[5]", 4, "DamageInfo:GetDamageType value" );
}

// Pins: NULL inflictor/attacker map to nil accessors.
TEST(ETD_nil_mapping)
{
	LUA( "hook.Add('EntityTakeDamage', 'rec', function(ent, info)\n"
		"	Nils = { info:GetAttacker() == nil, info:GetInflictor() == nil }\n"
		"end)\n"
		"etd(1, -1, -1, 5, 1)" );
	CHECK_LUA_BOOL( "Nils[1]", true, "DamageInfo: NULL attacker -> nil" );
	CHECK_LUA_BOOL( "Nils[2]", true, "DamageInfo: NULL inflictor -> nil" );
}

// Pins: only strict true cancels; modifications apply on every continue.
TEST(ETD_cancel_matrix)
{
	static const char *rets[] = { "true", "false", "nil", "1", "'yes'", NULL };
	static const int cancel[] = { 1, 0, 0, 0, 0 };
	for( int i = 0; rets[i]; ++i )
	{
		char code[200], desc[96];
		snprintf( code, sizeof( code ),
			"hook.Add('EntityTakeDamage', 't', function(ent, info) info:SetDamage(99) return %s end)", rets[i] );
		LUA( code );
		LUA( "Cancel, Dmg = etd(1, -1, -1, 10, 1)" );
		snprintf( desc, sizeof( desc ), "EntityTakeDamage: return %s -> cancel=%d", rets[i], cancel[i] );
		CHECK_LUA_INT( "Cancel", cancel[i], desc );
		CHECK_LUA_FLT( "Dmg", 99, "EntityTakeDamage: SetDamage wrote through" );
		LUA( "hook.Remove('EntityTakeDamage', 't')" );
	}
}

// Pins: GM fallback cancels on true; hook false suppresses the GM cancel.
TEST(ETD_gm_cancel_priority)
{
	LUA( "GAMEMODE = { EntityTakeDamage = function(gm, ent, info) return true end }" );
	CHECK_LUA_INT( "select(1, etd(1, -1, -1, 10, 1))", 1, "EntityTakeDamage: GM true cancels" );
	LUA( "hook.Add('EntityTakeDamage', 't', function() return false end)" );
	CHECK_LUA_INT( "select(1, etd(1, -1, -1, 10, 1))", 0, "EntityTakeDamage: hook false suppresses GM cancel" );
	LUA( "hook.Remove('EntityTakeDamage', 't'); GAMEMODE = nil" );
}

// Pins: a GM error rolls back modifications and continues native damage.
TEST(ETD_gm_error_rollback)
{
	LUA( "GAMEMODE = { EntityTakeDamage = function(gm, ent, info) info:SetDamage(50); error('rollback') end }" );
	LUA( "Cancel, Dmg, Type = etd(1, -1, -1, 5, 1)" );
	CHECK_LUA_INT( "Cancel", 0, "EntityTakeDamage: GM error continues" );
	CHECK_LUA_FLT( "Dmg", 5, "EntityTakeDamage: GM error rolls back SetDamage" );
	CHECK_LUA_INT( "Type", 1, "EntityTakeDamage: type unchanged after rollback" );
	CHECK_LUA_INT( "select(1, errors())", 1, "EntityTakeDamage: GM error reported once" );
	LUA( "GAMEMODE = nil" );
}

// Pins: SetDamage rejects non-finite / out-of-float-range values.
TEST(ETD_setdamage_validation)
{
	LUA( "hook.Add('EntityTakeDamage', 'v', function(ent, info)\n"
		"	OkNaN = pcall( info.SetDamage, info, 0/0 )\n"
		"	OkInf = pcall( info.SetDamage, info, 1/0 )\n"
		"	OkBig = pcall( info.SetDamage, info, 1e300 )\n"
		"	info:SetDamage( 3.5 )\n"
		"end)\n"
		"Cancel, Dmg = etd(1, -1, -1, 5, 1)" );
	CHECK_LUA_BOOL( "OkNaN", false, "DamageInfo:SetDamage NaN rejected" );
	CHECK_LUA_BOOL( "OkInf", false, "DamageInfo:SetDamage inf rejected" );
	CHECK_LUA_BOOL( "OkBig", false, "DamageInfo:SetDamage beyond FLT_MAX rejected" );
	CHECK_LUA_FLT( "Dmg", 3.5, "DamageInfo:SetDamage valid value still applies" );
}

// Pins: SetDamageType requires an integer within INT_MIN..INT_MAX.
TEST(ETD_setdamagetype_validation)
{
	LUA( "hook.Add('EntityTakeDamage', 'v', function(ent, info)\n"
		"	OkInf = pcall( info.SetDamageType, info, 1/0 )\n"
		"	OkFrac = pcall( info.SetDamageType, info, 1.5 )\n"
		"	OkBig = pcall( info.SetDamageType, info, 2147483648 )\n"
		"	OkMin = pcall( info.SetDamageType, info, -2147483648 )\n"
		"	OkMax = pcall( info.SetDamageType, info, 2147483647 )\n"
		"	info:SetDamageType( -2147483648 )\n"
		"end)\n"
		"Cancel, Dmg, Type = etd(1, -1, -1, 5, 1)" );
	CHECK_LUA_BOOL( "OkInf", false, "DamageInfo:SetDamageType inf rejected" );
	CHECK_LUA_BOOL( "OkFrac", false, "DamageInfo:SetDamageType fractional rejected" );
	CHECK_LUA_BOOL( "OkBig", false, "DamageInfo:SetDamageType above INT_MAX rejected" );
	CHECK_LUA_BOOL( "OkMin", true, "DamageInfo:SetDamageType INT_MIN accepted" );
	CHECK_LUA_BOOL( "OkMax", true, "DamageInfo:SetDamageType INT_MAX accepted" );
	CHECK_LUA_INT( "Type", -2147483648, "DamageInfo:SetDamageType negative value writes through" );
}

// Pins: a kept DamageInfo raises explicitly after its dispatch ended.
TEST(ETD_lifetime_stale)
{
	LUA( "hook.Add('EntityTakeDamage', 'keep', function(ent, info) Kept = info end)\n"
		"etd(1, -1, -1, 5, 1)\n"
		"Ok, Err = pcall( function() return Kept:GetDamage() end )" );
	CHECK_LUA_BOOL( "Ok", false, "DamageInfo: stale GetDamage raises" );
	CHECK_LUA_BOOL( "Err:find('outside its EntityTakeDamage', 1, true) ~= nil", true, "DamageInfo: stale error is explicit" );
	LUA( "OkSet = pcall( function() Kept:SetDamage( 1 ) end )" );
	CHECK_LUA_BOOL( "OkSet", false, "DamageInfo: stale SetDamage raises too" );
}

// Pins: a later dispatch invalidates earlier kept userdata (ABA guard).
TEST(ETD_lifetime_sequential)
{
	LUA( "hook.Add('EntityTakeDamage', 'aba', function(ent, info)\n"
		"	if Saved then AbaOk = pcall( function() return Saved:GetDamage() end ) else Saved = info end\n"
		"end)\n"
		"etd(1, -1, -1, 5, 1); etd(1, -1, -1, 6, 1)" );
	CHECK_LUA_BOOL( "AbaOk", false, "DamageInfo: first userdata invalid on the next dispatch" );
}

// Pins: GetAttacker/GetInflictor re-validate serial/free/KILLME at access.
TEST(ETD_attacker_validity)
{
	LUA( "hook.Add('EntityTakeDamage', 'val', function(ent, info)\n"
		"	bumpserial(2)\n"
		"	SerialNil = info:GetAttacker() == nil\n"
		"end)\n"
		"etd(1, 3, 2, 5, 1)" );
	CHECK_LUA_BOOL( "SerialNil", true, "DamageInfo:GetAttacker serial mismatch -> nil" );
	LUA( "hook.Remove('EntityTakeDamage', 'val')\n"
		"hook.Add('EntityTakeDamage', 'val', function(ent, info)\n"
		"	freeent(3)\n"
		"	FreeNil = info:GetInflictor() == nil\n"
		"end)\n"
		"etd(1, 3, -1, 5, 1)" );
	CHECK_LUA_BOOL( "FreeNil", true, "DamageInfo:GetInflictor freed -> nil" );
	LUA( "hook.Remove('EntityTakeDamage', 'val')\n"
		"resetent(2)\n"
		"hook.Add('EntityTakeDamage', 'val', function(ent, info)\n"
		"	killme(2)\n"
		"	KillNil = info:GetAttacker() == nil\n"
		"end)\n"
		"etd(1, -1, 2, 5, 1)" );
	CHECK_LUA_BOOL( "KillNil", true, "DamageInfo:GetAttacker FL_KILLME -> nil" );
}

// Pins: nested dispatch saves/restores the live slot; inner userdata is
// stale afterwards, the outer one works again.
TEST(ETD_nested_dispatch)
{
	LUA( "hook.Add('EntityTakeDamage', 'outer', function(ent, info)\n"
		"	if InNest then return end\n"
		"	InNest = true\n"
		"	OuterInfo = info\n"
		"	BeforeNest = info:GetDamage()\n"
		"	NestCancel, NestDamage = nest()\n"
		"	AfterNest = info:GetDamage()\n"
		"	InnerStale = not pcall( function() return InnerInfo:GetDamage() end )\n"
		"	InNest = false\n"
		"end)\n"
		"hook.Add('EntityTakeDamage', 'inner', function(ent, info)\n"
		"	if InNest then\n"
		"		InnerInfo = info\n"
		"		OuterStaleDuring = not pcall( function() return OuterInfo:GetDamage() end )\n"
		"	end\n"
		"end)\n"
		"etd(1, -1, -1, 25, 1)" );
	CHECK_LUA_FLT( "BeforeNest", 25, "nested: outer info live before the inner dispatch" );
	CHECK_LUA_INT( "NestCancel", 0, "nested: inner dispatch ran" );
	CHECK_LUA_FLT( "NestDamage", 7, "nested: inner damage value" );
	CHECK_LUA_FLT( "AfterNest", 25, "nested: outer info live again after the inner dispatch" );
	CHECK_LUA_BOOL( "OuterStaleDuring", true, "nested: outer info invalid during the inner dispatch" );
	CHECK_LUA_BOOL( "InnerStale", true, "nested: inner info stale after it returned" );
}

// Pins: accessors enforce the DamageInfo self type.
TEST(ETD_wrong_self)
{
	LUA( "hook.Add('EntityTakeDamage', 'ws', function(ent, info)\n"
		"	WrongSelf = pcall( function() return info.GetDamage( {} ) end )\n"
		"	NumberSelf = pcall( function() return info.GetDamage( 42 ) end )\n"
		"end)\n"
		"etd(1, -1, -1, 5, 1)" );
	CHECK_LUA_BOOL( "WrongSelf", false, "DamageInfo: table self rejected" );
	CHECK_LUA_BOOL( "NumberSelf", false, "DamageInfo: number self rejected" );
}

// Pins: exactly one dispatch per call across repeated mixed events.
TEST(ETD_dispatch_counts)
{
	LUA( "UseN, ShouldN, EntN = 0, 0, 0\n"
		"GAMEMODE = {\n"
		"	PlayerUse = function() UseN = UseN + 1 end,\n"
		"	PlayerShouldTakeDamage = function() ShouldN = ShouldN + 1 end,\n"
		"	EntityTakeDamage = function(gm, ent, info) EntN = EntN + 1 end }\n"
		"for i = 1, 500 do use(0, 1); should(0, -1); etd(1, -1, -1, 5, 1) end" );
	CHECK_LUA_INT( "UseN", 500, "PlayerUse: one dispatch per call" );
	CHECK_LUA_INT( "ShouldN", 500, "PlayerShouldTakeDamage: one dispatch per call" );
	CHECK_LUA_INT( "EntN", 500, "EntityTakeDamage: one dispatch per call" );
	CHECK_LUA_INT( "select(1, errors())", 0, "dispatch counts: no errors" );
}

int main()
{
	xtest::Setup() = SetupDamage;
	int rc = xtest::RunAll( "damage_hooks" );
	g_xglua_L = NULL;
	return rc;
}