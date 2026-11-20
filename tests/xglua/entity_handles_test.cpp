// Regression for entity handle identity, instance fields, SWEP class
// lookup and cache cleanup (dlls/lua/xglua_entity_handles.h +
// xglua_entity_methods.h, driven through the shared entity fixture).

#include "entity_fixture.h"

// Pins: one canonical userdata per edict index; table-key identity holds.
TEST(Handle_identity_cache)
{
	CHECK_LUA_BOOL( "Entity(1) == Entity(1)", true, "Entity: repeated acquisition is identical" );
	CHECK_LUA_BOOL( "slot(1) == Entity(1)", true, "Entity: pusher paths share the cache" );
	CHECK_LUA_BOOL( "Entity(1) == Entity(2)", false, "Entity: distinct slots are distinct handles" );
	CHECK_LUA_BOOL( "({ [Entity(1)] = 'ok' })[Entity(1)]", true, "Entity: usable as table key" );
	CHECK_LUA_BOOL( "Entity(0) == game.GetWorld()", true, "Entity: world handle identity" );
}

// Pins: instance fields live in the handle fenv and are shared by every
// acquisition of the same entity.
TEST(Handle_instance_fields)
{
	LUA( "A = Entity(1); B = Entity(1)" );
	LUA( "A.score = 42" );
	CHECK_LUA_INT( "B.score", 42, "Entity field: visible through the second handle" );
	LUA( "A.payload = { n = 7 }" );
	CHECK_LUA_INT( "B.payload.n", 7, "Entity field: table value shared" );
	CHECK_LUA_NIL( "Entity(2).score", "Entity field: not leaked to other entities" );
	LUA( "A.score = nil" );
	CHECK_LUA_NIL( "A.score", "Entity field: cleared back to nil" );
}

// Pins: instance fields shadow metatable methods; clearing restores them.
TEST(Handle_method_shadowing)
{
	LUA( "A = Entity(1); B = Entity(1)" );
	CHECK_LUA_STR( "A:GetClass()", "player", "Entity method: native GetClass before shadowing" );
	LUA( "A.GetClass = 42" );
	CHECK_LUA_INT( "B.GetClass", 42, "Entity field: shadows the metatable method" );
	LUA( "A.GetClass = nil" );
	CHECK_LUA_STR( "A:GetClass()", "player", "Entity method: restored after clearing the field" );
}

// Pins: Lua-weapon entities resolve class fields through the SWEP registry,
// including __index inheritance; native entities do not.
TEST(Handle_swep_class_lookup)
{
	LUA( "swepSet( 'weapon_lua_test', setmetatable( { ClassValue = 'class' }, { __index = { InheritedValue = 'base' } } ) )" );
	LUA( "W = slot(4); N = slot(5)" );
	CHECK_LUA_STR( "W.ClassValue", "class", "SWEP lookup: own class field" );
	CHECK_LUA_STR( "W.InheritedValue", "base", "SWEP lookup: inherited via __index" );
	CHECK_LUA_NIL( "N.ClassValue", "SWEP lookup: native weapon has no class table" );
	LUA( "W.ClassValue = 'instance'" );
	CHECK_LUA_STR( "W.ClassValue", "instance", "SWEP lookup: instance field wins over class" );
	LUA( "W.ClassValue = nil" );
	CHECK_LUA_STR( "W.ClassValue", "class", "SWEP lookup: clearing instance falls back to class" );
}

// Pins: SWEP methods beat metatable methods; instance fields beat both.
TEST(Handle_swep_method_priority)
{
	LUA( "swepSet( 'weapon_lua_test', { GetClass = function() return 'swep' end } )" );
	CHECK_LUA_STR( "slot(4):GetClass()", "swep", "SWEP method: overrides the metatable method" );
	CHECK_LUA_STR( "slot(5):GetClass()", "weapon_9mmhandgun", "SWEP method: native entity keeps the metatable method" );
	LUA( "local w = slot(4); w.GetClass = function() return 'instance' end" );
	CHECK_LUA_STR( "slot(4):GetClass()", "instance", "SWEP method: instance field still wins" );
}

// Pins: __index on a non-entity self raises "Expected an entity!".
TEST(Handle_index_non_entity)
{
	CHECK_LUA_ERROR( "return getmetatable(Entity(1)).__index({}, 'x')", "Expected an entity!",
		"Ent_Index: table self rejected" );
	CHECK_LUA_ERROR( "return getmetatable(Entity(1)).__index(42, 'x')", "Expected an entity!",
		"Ent_Index: number self rejected" );
}

// Pins: a freed handle keeps IsValid callable; every other access raises.
TEST(Handle_stale_access)
{
	LUA( "Old = Entity(1); invalidate(1)" );
	CHECK_LUA_BOOL( "IsValid(Old)", false, "stale handle: global IsValid is false" );
	CHECK_LUA_BOOL( "Old:IsValid()", false, "stale handle: method IsValid stays callable" );
	CHECK_LUA_ERROR( "return Old.score", "Tried to use a NULL entity!", "stale handle: field read raises" );
	CHECK_LUA_ERROR( "Old.score = 1", "Tried to use a NULL entity!", "stale handle: field write raises" );
	CHECK_LUA_ERROR( "return Old:GetClass()", "Tried to use a NULL entity!", "stale handle: method call raises" );
	CHECK_LUA_ERROR( "return Old.IsValid2", "Tried to use a NULL entity!", "stale handle: only the exact IsValid key is allowed" );
}

// Pins: serial bump replaces the cache record; the old handle stays dead.
TEST(Handle_serial_reuse)
{
	LUA( "Old = Entity(1); Old.mark = 'old'; invalidate(1); reuse(1)" );
	LUA( "Fresh = Entity(1)" );
	CHECK_LUA_BOOL( "Fresh == Old", false, "serial reuse: new handle object" );
	CHECK_LUA_BOOL( "IsValid(Fresh)", true, "serial reuse: new handle valid" );
	CHECK_LUA_BOOL( "IsValid(Old)", false, "serial reuse: old handle stays invalid" );
	CHECK_LUA_NIL( "Fresh.mark", "serial reuse: instance fields do not survive" );
	LUA( "Fresh.mark = 'fresh'" );
	CHECK_LUA_STR( "Entity(1).mark", "fresh", "serial reuse: cache serves the new record" );
}

// Pins: the amortized sweep drops stale cache records so held fields can
// be collected (weak-value witness).
TEST(Handle_sweep_cleanup)
{
	LUA( "Weak = setmetatable({}, { __mode = 'v' })\n"
		"local doomed = Entity(3)\n"
		"local payload = {}\n"
		"Weak[1] = payload\n"
		"doomed.payload = payload\n"
		"doomed, payload = nil, nil\n"
		"invalidate(3)\n"
		"sweep(16)\n"
		"collectgarbage(); collectgarbage()" );
	CHECK_LUA_NIL( "Weak[1]", "sweep: stale record released, payload collected" );
	CHECK_LUA_BOOL( "IsValid(Entity(1))", true, "sweep: live records untouched" );
}

// Pins: sweep cursor normalization and budget caps; NULL args are safe.
TEST(Handle_sweep_cursor_bounds)
{
	LUA( "local a = Entity(1); invalidate(1)" );
	int cursor = 999;
	XGLua_SweepEntityHandles( L, FX_SLOTS, 4, &cursor );
	CHECK_EQ_INT( cursor, 4, "sweep: out-of-range cursor normalized then advanced by budget" );
	cursor = -5;
	XGLua_SweepEntityHandles( L, FX_SLOTS, 2, &cursor );
	CHECK_EQ_INT( cursor, 2, "sweep: negative cursor normalized to 0" );
	XGLua_SweepEntityHandles( L, FX_SLOTS, 1000, &cursor );
	CHECK_EQ_INT( cursor, 2, "sweep: budget capped at maxSlots wraps to the same cursor" );
	XGLua_SweepEntityHandles( NULL, FX_SLOTS, 8, &cursor );
	XGLua_SweepEntityHandles( L, 0, 8, &cursor );
	XGLua_SweepEntityHandles( L, FX_SLOTS, 0, &cursor );
	XGLua_SweepEntityHandles( L, FX_SLOTS, 8, NULL );
	CHECK_EQ_INT( cursor, 2, "sweep: invalid arguments leave the cursor untouched" );
	lua_getfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
	lua_rawgeti( L, -1, 1 );
	CHECK( lua_isnil( L, -1 ), "sweep: stale slot 1 record removed from the cache" );
	lua_pop( L, 2 );
}

// Pins: a fresh VM has no entity cache before the first acquisition.
TEST(Handle_fresh_vm_clean)
{
	lua_getfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
	CHECK( lua_isnil( L, -1 ), "fresh VM: no cache table before first push" );
	lua_pop( L, 1 );
	LUA( "local e = Entity(1)" );
	lua_getfield( L, LUA_REGISTRYINDEX, XGLUA_ENTITY_CACHE );
	CHECK( lua_istable( L, -1 ), "fresh VM: cache created lazily on first push" );
	lua_pop( L, 1 );
}

// Pins: IsValid accepts any value and is false for non-entities.
TEST(IsValid_non_entities)
{
	CHECK_LUA_BOOL( "IsValid(nil)", false, "IsValid: nil" );
	CHECK_LUA_BOOL( "IsValid(false)", false, "IsValid: boolean" );
	CHECK_LUA_BOOL( "IsValid(123)", false, "IsValid: number" );
	CHECK_LUA_BOOL( "IsValid({})", false, "IsValid: table" );
	CHECK_LUA_BOOL( "IsValid(io.stdout)", false, "IsValid: foreign userdata" );
	CHECK_LUA_BOOL( "IsValid(Entity(1))", true, "IsValid: live entity" );
}

// Pins: instance fields survive GC of every Lua-side handle reference.
TEST(Handle_fields_survive_gc)
{
	LUA( "do local e = Entity(1); e.value = { n = 42 } end\n"
		"collectgarbage(); collectgarbage()" );
	CHECK_LUA_INT( "Entity(1).value.n", 42, "Entity field: survives handle collection" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_handles" );
}