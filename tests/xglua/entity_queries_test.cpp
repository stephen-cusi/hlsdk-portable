// Regression for the query APIs (dlls/lua/xglua_entity_queries.h):
// ents.FindByName/FindInSphere/FindInBox, player.GetAll/GetBots/GetHumans/
// GetByUserID, ply:UserID, game.GetMap/MaxPlayers/GetWorld.

#include "entity_fixture.h"

// Pins: FindByName exact case-sensitive targetname match, world excluded.
TEST(Queries_findbyname_exact)
{
	LUA( "settargetname(1, 'door1'); settargetname(2, 'door1'); settargetname(0, 'worldname')" );
	LUA( "M = ents.FindByName('door1')" );
	CHECK_LUA_INT( "#M", 2, "FindByName: both matches" );
	CHECK_LUA_BOOL( "M[1] == Entity(1) and M[2] == Entity(2)", true, "FindByName: slot order" );
	CHECK_LUA_INT( "#ents.FindByName('DOOR1')", 0, "FindByName: case sensitive" );
	CHECK_LUA_INT( "#ents.FindByName('worldname')", 0, "FindByName: world index 0 never scanned" );
	CHECK_LUA_INT( "#ents.FindByName('')", 0, "FindByName: empty string returns empty table" );
	CHECK_LUA_INT( "#ents.FindByName('nothing')", 0, "FindByName: no match" );
}

// Pins: FindByName argument validation (NUL byte, non-string).
TEST(Queries_findbyname_validation)
{
	CHECK_LUA_ERROR( "return ents.FindByName('bad\\0name')", "targetname contains a NUL byte", "FindByName: embedded NUL rejected" );
	CHECK_LUA_ERROR( "return ents.FindByName({})", "string expected", "FindByName: table argument raises" );
	CHECK_LUA_ERROR( "return ents.FindByName(42)", "string expected", "FindByName: number rejected (checktype, not checkstring)" );
}

// Pins: FindByName skips freed, KILLME-marked, uninitialized and unnamed.
TEST(Queries_findbyname_filters)
{
	LUA( "settargetname(1, 'door1'); settargetname(2, 'door1')" );
	LUA( "invalidate(2)" );
	CHECK_LUA_INT( "#ents.FindByName('door1')", 1, "FindByName: freed entity skipped" );
	LUA( "reuse(2); setflags(2, 1073741824)" );
	CHECK_LUA_INT( "#ents.FindByName('door1')", 1, "FindByName: FL_KILLME skipped" );
	LUA( "clearflags(2, 1073741824); setpriv(2, false)" );
	CHECK_LUA_INT( "#ents.FindByName('door1')", 1, "FindByName: missing private data skipped" );
	LUA( "setpriv(2, true); settargetname(2, nil)" );
	CHECK_LUA_INT( "#ents.FindByName('door1')", 1, "FindByName: null targetname skipped" );
}

// Pins: FindInSphere uses AABB nearest-point distance, touching counts.
TEST(Queries_findinsphere_match)
{
	LUA( "setabs(1, 0,0,0, 10,10,10); setabs(2, 20,20,20, 30,30,30)" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,5,5), 10)", 1, "FindInSphere: nearest box only" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(5,5,5), 0)", 1, "FindInSphere: radius 0 point inside box" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,5,5), 4)", 0, "FindInSphere: distance 5 > radius 4" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,5,5), 5)", 1, "FindInSphere: distance == radius counts" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,15,15), 10)", 2, "FindInSphere: corner touches both boxes" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,5,5), 1e60)", 5, "FindInSphere: huge radius capped, all five entities, no overflow" );
}

// Pins: FindInSphere skips entities with non-finite or unordered bounds.
TEST(Queries_findinsphere_entity_bounds)
{
	LUA( "setabs(1, 0,0,0, 10,10,10); setabs(2, 30,30,30, 20,20,20)" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(15,5,5), 1e60)", 4, "FindInSphere: unordered entity bounds skipped" );
	LUA( "setabs(2, 0/0,0,0, 10,10,10)" );
	CHECK_LUA_INT( "#ents.FindInSphere(Vector(5,5,5), 100)", 1, "FindInSphere: non-finite entity bounds skipped" );
}

// Pins: FindInSphere argument validation.
TEST(Queries_findinsphere_validation)
{
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(0/0,0,0), 5)", "center must be finite", "FindInSphere: NaN center rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(1/0,0,0), 5)", "center must be finite", "FindInSphere: inf center rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(0,0,0), -1)", "radius must be finite and non-negative", "FindInSphere: negative radius rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(0,0,0), 0/0)", "radius must be finite and non-negative", "FindInSphere: NaN radius rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(0,0,0), 1/0)", "radius must be finite and non-negative", "FindInSphere: inf radius rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere(Vector(0,0,0), 'x')", "number expected", "FindInSphere: string radius rejected" );
	CHECK_LUA_ERROR( "return ents.FindInSphere('nope', 5)", "XGLua_Vector expected", "FindInSphere: non-vector center rejected" );
}

// Pins: FindInBox overlap including touching; disjoint boxes excluded.
TEST(Queries_findinbox_match)
{
	LUA( "setabs(1, 5,5,5, 15,15,15); setabs(2, 20,20,20, 30,30,30)" );
	CHECK_LUA_INT( "#ents.FindInBox(Vector(0,0,0), Vector(10,10,10))", 1, "FindInBox: partial overlap" );
	CHECK_LUA_INT( "#ents.FindInBox(Vector(10,10,10), Vector(20,20,20))", 2, "FindInBox: touching counts for both" );
	CHECK_LUA_INT( "#ents.FindInBox(Vector(40,0,0), Vector(50,10,10))", 0, "FindInBox: disjoint excluded" );
	LUA( "setabs(1, -1/0,0,0, 10,10,10)" );
	CHECK_LUA_INT( "#ents.FindInBox(Vector(0,0,0), Vector(10,10,10))", 0, "FindInBox: non-finite entity bounds skipped" );
}

// Pins: FindInBox query bounds must be finite and ordered.
TEST(Queries_findinbox_validation)
{
	CHECK_LUA_ERROR( "return ents.FindInBox(Vector(10,0,0), Vector(0,10,10))", "query mins must not exceed maxs", "FindInBox: unordered query rejected" );
	CHECK_LUA_ERROR( "return ents.FindInBox(Vector(0/0,0,0), Vector(10,10,10))", "query bounds must be finite", "FindInBox: NaN mins rejected" );
	CHECK_LUA_ERROR( "return ents.FindInBox(Vector(0,0,0), Vector(1/0,10,10))", "query bounds must be finite", "FindInBox: inf maxs rejected" );
	LUA( "setabs(1, 5,5,5, 15,15,15)" );
	CHECK_LUA_INT( "#ents.FindInBox(Vector(5,5,5), Vector(5,5,5))", 1, "FindInBox: degenerate point query on boundary" );
}

// Pins: player lists split by FL_FAKECLIENT within maxClients.
TEST(Queries_player_lists)
{
	CHECK_LUA_INT( "#player.GetHumans()", 1, "GetHumans: one human" );
	CHECK_LUA_BOOL( "player.GetHumans()[1] == Entity(1)", true, "GetHumans: identity" );
	CHECK_LUA_INT( "#player.GetBots()", 1, "GetBots: one bot" );
	CHECK_LUA_BOOL( "player.GetBots()[1] == Entity(2)", true, "GetBots: identity" );
	CHECK_LUA_INT( "#player.GetAll()", 2, "GetAll: humans plus bots" );
	LUA( "clearflags(2, 8192)" );
	CHECK_LUA_INT( "#player.GetHumans()", 2, "GetHumans: bot flag cleared moves it to humans" );
	CHECK_LUA_INT( "#player.GetBots()", 0, "GetBots: none left" );
	LUA( "setflags(2, 8192); maxclients(1)" );
	CHECK_LUA_INT( "#player.GetBots()", 0, "GetBots: slots beyond maxClients not scanned" );
	LUA( "maxclients(4); invalidate(2)" );
	CHECK_LUA_INT( "#player.GetAll()", 1, "GetAll: freed player skipped" );
}

// Pins: UserID returns positive engine ids, nil for 0/-1/missing callback.
TEST(Queries_userid)
{
	CHECK_LUA_INT( "slot(1):UserID()", 5, "UserID: engine id" );
	CHECK_LUA_INT( "slot(2):UserID()", 6, "UserID: second player" );
	LUA( "setuid(2, 0)" );
	CHECK_LUA_NIL( "slot(2):UserID()", "UserID: zero id becomes nil" );
	LUA( "setuid(2, -1)" );
	CHECK_LUA_NIL( "slot(2):UserID()", "UserID: negative id becomes nil" );
	LUA( "useridcb(false)" );
	CHECK_LUA_NIL( "slot(1):UserID()", "UserID: missing engine callback becomes nil" );
	LUA( "useridcb(true); setuid(2, 6)" );
}

// Pins: GetByUserID finds the live player, nil for unknown ids.
TEST(Queries_getbyuserid)
{
	CHECK_LUA_BOOL( "player.GetByUserID(5) == Entity(1)", true, "GetByUserID: finds player 1" );
	CHECK_LUA_BOOL( "player.GetByUserID(6) == Entity(2)", true, "GetByUserID: finds player 2" );
	CHECK_LUA_NIL( "player.GetByUserID(7)", "GetByUserID: unknown id nil" );
	LUA( "setuid(1, 2147483647)" );
	CHECK_LUA_BOOL( "player.GetByUserID(2147483647) == Entity(1)", true, "GetByUserID: INT_MAX id accepted" );
	LUA( "setuid(1, 5); invalidate(1)" );
	CHECK_LUA_NIL( "player.GetByUserID(5)", "GetByUserID: freed player skipped without error" );
	LUA( "reuse(1)" );
}

// Pins: GetByUserID rejects non-positive-integer ids.
TEST(Queries_getbyuserid_validation)
{
	CHECK_LUA_ERROR( "return player.GetByUserID(0)", "user ID must be a positive integer", "GetByUserID: zero rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID(-3)", "user ID must be a positive integer", "GetByUserID: negative rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID(1.5)", "user ID must be a positive integer", "GetByUserID: fractional rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID(0/0)", "user ID must be a positive integer", "GetByUserID: NaN rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID(1/0)", "user ID must be a positive integer", "GetByUserID: inf rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID(2147483648)", "user ID must be a positive integer", "GetByUserID: above INT_MAX rejected" );
	CHECK_LUA_ERROR( "return player.GetByUserID('x')", "number expected", "GetByUserID: string rejected" );
}

// Pins: game.GetMap/MaxPlayers/GetWorld.
TEST(Queries_game_lib)
{
	CHECK_LUA_STR( "game.GetMap()", "", "game.GetMap: null mapname becomes empty string" );
	LUA( "setmap('bounce')" );
	CHECK_LUA_STR( "game.GetMap()", "bounce", "game.GetMap: current map" );
	CHECK_LUA_INT( "game.MaxPlayers()", 4, "game.MaxPlayers: maxClients" );
	CHECK_LUA_BOOL( "game.GetWorld() == Entity(0)", true, "game.GetWorld: world handle identity" );
	CHECK_LUA_INT( "game.GetWorld():EntIndex()", 0, "game.GetWorld: index 0" );
}

// Pins: stale handles raise on direct reads but scans skip them silently.
TEST(Queries_stale_paths)
{
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "return Stale:UserID()", "Tried to use a NULL entity!", "stale: UserID raises" );
	CHECK_LUA_INT( "#player.GetHumans()", 0, "stale: human scan skips freed slot" );
	CHECK_LUA_INT( "#ents.FindByName('x')", 0, "stale: name scan skips freed slot" );
	LUA( "reuse(1)" );
	CHECK_LUA_INT( "slot(1):UserID()", 5, "stale: reused slot readable again" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_queries" );
}