// Regression for entity/player state reads (dlls/lua/xglua_entity_state.h):
// movetype/solid/waterlevel, OnGround flag, Crouching, Armor, Ping engine
// stats, and the KeyDown/KeyPressed/KeyReleased 16-bit button masks.

#include "entity_fixture.h"

// Pins: movetype/solid/waterlevel are raw pev integers.
TEST(State_native_integers)
{
	LUA( "setmovetype(1, 5); setsolid(0, 3); setwater(1, 2)" );
	CHECK_LUA_INT( "slot(1):GetMoveType()", 5, "GetMoveType: raw movetype" );
	CHECK_LUA_INT( "slot(0):GetSolid()", 3, "GetSolid: raw solid (works on world)" );
	CHECK_LUA_INT( "slot(1):WaterLevel()", 2, "WaterLevel: raw waterlevel" );
	CHECK_LUA_INT( "slot(2):WaterLevel()", 0, "WaterLevel: default zero" );
}

// Pins: OnGround/IsOnGround test FL_ONGROUND on ANY entity.
TEST(State_onground)
{
	CHECK_LUA_BOOL( "slot(1):OnGround()", false, "OnGround: false without the flag" );
	LUA( "setflags(1, 512)" );
	CHECK_LUA_BOOL( "slot(1):OnGround()", true, "OnGround: FL_ONGROUND set" );
	CHECK_LUA_BOOL( "slot(1):IsOnGround()", true, "IsOnGround: alias of OnGround" );
	LUA( "setflags(0, 512)" );
	CHECK_LUA_BOOL( "slot(0):OnGround()", true, "OnGround: works on non-player entities" );
	LUA( "clearflags(1, 512)" );
	CHECK_LUA_BOOL( "slot(1):OnGround()", false, "OnGround: cleared flag" );
}

// Pins: Crouching tests FL_DUCKING and requires a player.
TEST(State_crouching)
{
	CHECK_LUA_BOOL( "slot(1):Crouching()", false, "Crouching: false standing" );
	LUA( "setflags(1, 16384)" );
	CHECK_LUA_BOOL( "slot(1):Crouching()", true, "Crouching: FL_DUCKING set" );
	CHECK_LUA_ERROR( "return slot(3):Crouching()", "non-player entity as a Player", "Crouching: non-player raises" );
}

// Pins: Armor keeps fractional values.
TEST(State_armor)
{
	LUA( "setarmor(1, 25.5)" );
	CHECK_LUA_FLT( "slot(1):Armor()", 25.5, "Armor: fractional value preserved" );
	CHECK_LUA_FLT( "slot(2):Armor()", 0, "Armor: default zero" );
}

// Pins: Ping is 0 without the engine callback, else the reported value for
// the right edict.
TEST(State_ping)
{
	LUA( "statscb(false)" );
	CHECK_LUA_INT( "slot(1):Ping()", 0, "Ping: engine stats unavailable -> 0" );
	LUA( "statscb(true)" );
	CHECK_LUA_INT( "slot(1):Ping()", 42, "Ping: engine-reported value" );
	CHECK_LUA_INT( "statsent()", 1, "Ping: stats queried for the player edict" );
	LUA( "statscb(false)" );
	CHECK_LUA_INT( "slot(1):Ping()", 0, "Ping: back to 0 when the callback disappears" );
}

// Pins: KeyDown matches any common bit between pev->button and the mask.
TEST(Buttons_keydown)
{
	LUA( "setbutton(1, 3)" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(1)", true, "KeyDown: bit 1 held" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(2)", true, "KeyDown: bit 2 held" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(4)", false, "KeyDown: bit 4 not held" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(3)", true, "KeyDown: multi-bit mask matches any" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(0)", false, "KeyDown: zero mask never matches" );
	LUA( "setbutton(1, 0)" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(65535)", false, "KeyDown: full mask vs empty buttons" );
}

// Pins: KeyPressed/KeyReleased read the PreThink snapshot fields.
TEST(Buttons_pressed_released)
{
	LUA( "setpressed(1, 1); setreleased(1, 4)" );
	CHECK_LUA_BOOL( "slot(1):KeyPressed(1)", true, "KeyPressed: snapshot bit set" );
	CHECK_LUA_BOOL( "slot(1):KeyPressed(2)", false, "KeyPressed: other bit clear" );
	CHECK_LUA_BOOL( "slot(1):KeyReleased(4)", true, "KeyReleased: snapshot bit set" );
	CHECK_LUA_BOOL( "slot(1):KeyReleased(1)", false, "KeyReleased: other bit clear" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(1)", false, "KeyDown: independent of the pressed snapshot" );
	LUA( "setpressed(1, 0); setreleased(1, 0)" );
	CHECK_LUA_BOOL( "slot(1):KeyPressed(1)", false, "KeyPressed: cleared snapshot" );
}

// Pins: button masks must be finite integers in 0..65535.
TEST(Buttons_mask_validation)
{
	CHECK_LUA_ERROR( "return slot(1):KeyDown(-1)", "16-bit unsigned integer button mask", "KeyDown: negative mask rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown(65536)", "16-bit unsigned integer button mask", "KeyDown: mask above 65535 rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown(1.5)", "16-bit unsigned integer button mask", "KeyDown: fractional mask rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown(0/0)", "16-bit unsigned integer button mask", "KeyDown: NaN rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown(1/0)", "16-bit unsigned integer button mask", "KeyDown: inf rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown('x')", "number expected", "KeyDown: string rejected" );
	CHECK_LUA_ERROR( "return slot(1):KeyDown()", "number expected", "KeyDown: missing mask rejected" );
	CHECK_LUA_BOOL( "slot(1):KeyDown(65535)", false, "KeyDown: 65535 is a valid mask" );
	CHECK_LUA_ERROR( "return slot(1):KeyPressed(-1)", "16-bit unsigned integer button mask", "KeyPressed: same validation" );
	CHECK_LUA_ERROR( "return slot(1):KeyReleased(65536)", "16-bit unsigned integer button mask", "KeyReleased: same validation" );
}

// Pins: state reads raise on stale handles; player reads on non-players.
TEST(State_error_paths)
{
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "return Stale:GetMoveType()", "Tried to use a NULL entity!", "stale: GetMoveType raises" );
	CHECK_LUA_ERROR( "return Stale:OnGround()", "Tried to use a NULL entity!", "stale: OnGround raises" );
	CHECK_LUA_ERROR( "return slot(3):Armor()", "non-player entity as a Player", "non-player: Armor raises" );
	CHECK_LUA_ERROR( "return slot(3):Ping()", "non-player entity as a Player", "non-player: Ping raises" );
	CHECK_LUA_ERROR( "return slot(3):KeyDown(1)", "non-player entity as a Player", "non-player: KeyDown raises" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_state" );
}