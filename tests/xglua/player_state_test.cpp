// Regression for the player API surface (dlls/lua/xglua_player_api.h +
// xglua_player_extra.h): Name/Frags/Deaths/Team/IsBot/IsConnected, weapon
// inventory reads, and observer/FOV/train/use/vehicle/frozen/god state.

#include "entity_fixture.h"

// Pins: Name reads netname on any entity; Frags keeps fractions; Deaths is
// the player counter.
TEST(Player_identity_reads)
{
	LUA( "setnetname(1, 'Alice'); setfrags(1, 17.5); setdeaths(1, 3)" );
	CHECK_LUA_STR( "slot(1):Name()", "Alice", "Name: netname" );
	CHECK_LUA_NIL( "slot(0):Name()", "Name: NULL netname pushes nil (lua_pushstring(NULL))" );
	CHECK_LUA_FLT( "slot(1):Frags()", 17.5, "Frags: fractional score preserved" );
	CHECK_LUA_INT( "slot(1):Deaths()", 3, "Deaths: player death counter" );
	LUA( "setnetname(1, '')" );
	CHECK_LUA_STR( "slot(1):Name()", "", "Name: empty netname" );
}

// Pins: Team degrades NULL TeamID to ""; IsBot reads FL_FAKECLIENT.
TEST(Player_team_bot)
{
	LUA( "setteam(1, 'CT')" );
	CHECK_LUA_STR( "slot(1):Team()", "CT", "Team: native TeamID string" );
	LUA( "setteam(1, nil)" );
	CHECK_LUA_STR( "slot(1):Team()", "", "Team: NULL TeamID becomes empty string" );
	CHECK_LUA_BOOL( "slot(2):IsBot()", true, "IsBot: FL_FAKECLIENT set in fixture" );
	CHECK_LUA_BOOL( "slot(1):IsBot()", false, "IsBot: human player" );
	LUA( "clearflags(2, 8192)" );
	CHECK_LUA_BOOL( "slot(2):IsBot()", false, "IsBot: flag cleared" );
}

// Pins: IsConnected = live initialized client slot within maxClients.
TEST(Player_isconnected)
{
	CHECK_LUA_BOOL( "slot(1):IsConnected()", true, "IsConnected: live client slot" );
	CHECK_LUA_BOOL( "slot(0):IsConnected()", false, "IsConnected: world index 0 excluded" );
	LUA( "maxclients(1)" );
	CHECK_LUA_BOOL( "slot(2):IsConnected()", false, "IsConnected: index beyond maxClients" );
	LUA( "maxclients(4); setpriv(2, false)" );
	CHECK_LUA_BOOL( "slot(2):IsConnected()", false, "IsConnected: uninitialized private data" );
	LUA( "setpriv(2, true)" );
	CHECK_LUA_BOOL( "slot(2):IsConnected()", true, "IsConnected: restored private data" );
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "return Stale:IsConnected()", "Tried to use a NULL entity!", "IsConnected: stale handle raises" );
}

// Pins: empty inventory reads.
TEST(Player_weapons_empty)
{
	CHECK_LUA_BOOL( "slot(1):HasWeapons()", false, "HasWeapons: empty inventory" );
	CHECK_LUA_INT( "#slot(1):GetWeapons()", 0, "GetWeapons: empty array" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon('weapon_crowbar')", false, "HasWeapon: nothing owned" );
}

// Pins: GetWeapons walks slots 0..5 in chain order and returns valid handles.
TEST(Player_weapons_order)
{
	LUA( "local a = addweapon(1, 0, 'weapon_crowbar')\n"
		"local b = addweapon(1, 3, 'weapon_9mmhandgun')\n"
		"local c = addweapon(1, 3, 'weapon_shotgun')\n"
		"Weps = slot(1):GetWeapons()" );
	CHECK_LUA_BOOL( "slot(1):HasWeapons()", true, "HasWeapons: after giving weapons" );
	CHECK_LUA_INT( "#Weps", 3, "GetWeapons: dense array of three" );
	CHECK_LUA_BOOL( "Weps[1] == slot(8)", true, "GetWeapons: slot 0 weapon first" );
	CHECK_LUA_BOOL( "Weps[2] == slot(9)", true, "GetWeapons: same-slot chain order preserved" );
	CHECK_LUA_BOOL( "Weps[3] == slot(10)", true, "GetWeapons: appended item last" );
	CHECK_LUA_BOOL( "Weps[1]:IsValid() and Weps[2]:IsValid() and Weps[3]:IsValid()", true, "GetWeapons: all handles valid" );
}

// Pins: HasWeapon compares the native classname exactly.
TEST(Player_hasweapon_exact)
{
	LUA( "addweapon(1, 0, 'weapon_crowbar')" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon('weapon_crowbar')", true, "HasWeapon: exact match" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon('weapon_crowbar_extra')", false, "HasWeapon: no prefix matching" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon('Weapon_Crowbar')", false, "HasWeapon: case sensitive" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon('weapon_x')", false, "HasWeapon: unknown class" );
	CHECK_LUA_BOOL( "slot(1):HasWeapon(42)", false, "HasWeapon: number coerced to string, no match" );
	CHECK_LUA_ERROR( "return slot(1):HasWeapon({})", "string expected", "HasWeapon: table argument raises" );
}

// Pins: observer/FOV/train/use/vehicle/frozen/god read native state.
TEST(Player_extra_state)
{
	CHECK_LUA_BOOL( "slot(1):IsObserver()", false, "IsObserver: default false" );
	LUA( "setobserver(1, true)" );
	CHECK_LUA_BOOL( "slot(1):IsObserver()", true, "IsObserver: native observer flag" );
	LUA( "setfov(1, 90)" );
	CHECK_LUA_INT( "slot(1):GetFOV()", 90, "GetFOV: native FOV" );
	LUA( "setphysics(1, 2)" );
	CHECK_LUA_BOOL( "slot(1):IsOnTrain()", true, "IsOnTrain: PFLAG_ONTRAIN" );
	CHECK_LUA_BOOL( "slot(1):IsUsing()", false, "IsUsing: not set by ONTRAIN" );
	LUA( "setphysics(1, 18)" );
	CHECK_LUA_BOOL( "slot(1):IsOnTrain()", true, "IsOnTrain: combined flags" );
	CHECK_LUA_BOOL( "slot(1):IsUsing()", true, "IsUsing: PFLAG_USING" );
	LUA( "setphysics(1, 0); settank(1, true)" );
	CHECK_LUA_BOOL( "slot(1):InVehicle()", true, "InVehicle: m_pTank present" );
	LUA( "settank(1, false); setflags(1, 4096)" );
	CHECK_LUA_BOOL( "slot(1):IsFrozen()", true, "IsFrozen: FL_FROZEN" );
	LUA( "clearflags(1, 4096); setflags(1, 64)" );
	CHECK_LUA_BOOL( "slot(1):IsGodMode()", true, "IsGodMode: FL_GODMODE" );
}

// Pins: every player-only method rejects a non-player entity.
TEST(Player_nonplayer_errors)
{
	LUA( "Names = { 'Crouching','Armor','Ping','Frags','Deaths','Team','IsBot','HasWeapons',\n"
		"	'GetWeapons','IsObserver','GetFOV','IsOnTrain','IsUsing','InVehicle','IsFrozen','IsGodMode' }\n"
		"Bad = 0\n"
		"for _, name in ipairs( Names ) do\n"
		"	local ok = pcall( function() return slot(3)[name]( slot(3) ) end )\n"
		"	if ok then Bad = Bad + 1 end\n"
		"end" );
	CHECK_LUA_INT( "Bad", 0, "player methods: all 16 reject non-player self" );
	LUA( "Bad2 = 0\n"
		"for _, name in ipairs( { 'KeyDown','KeyPressed','KeyReleased' } ) do\n"
		"	local ok = pcall( function() return slot(3)[name]( slot(3), 1 ) end )\n"
		"	if ok then Bad2 = Bad2 + 1 end\n"
		"end" );
	CHECK_LUA_INT( "Bad2", 0, "button methods: reject non-player self" );
	CHECK_LUA_ERROR( "return slot(3):HasWeapon('x')", "non-player entity as a Player", "HasWeapon: non-player raises" );
}

// Pins: stale handles raise across the player API.
TEST(Player_stale_errors)
{
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "return Stale:Frags()", "Tried to use a NULL entity!", "stale: Frags raises" );
	CHECK_LUA_ERROR( "return Stale:GetWeapons()", "Tried to use a NULL entity!", "stale: GetWeapons raises" );
	CHECK_LUA_ERROR( "return Stale:IsGodMode()", "Tried to use a NULL entity!", "stale: IsGodMode raises" );
	CHECK_LUA_BOOL( "Stale:IsValid()", false, "stale: IsValid still answers false" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "player_state" );
}