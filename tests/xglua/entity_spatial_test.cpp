// Regression for world/entity spatial bindings (dlls/lua/xglua_entity_spatial.h):
// Entity(index) validation, ground entity, SetPos/SetCollisionBounds write-
// through and finite/ordering guards, ents.GetAll/FindByClass, Spawn/Remove
// lifecycle and the world-entity guards. Real XGLua_PushEntity is used.

#include "entity_fixture.h"

// Pins: Entity(index) returns the canonical handle for valid live indices.
TEST(Spatial_entity_valid)
{
	CHECK_LUA_BOOL( "Entity(0) == slot(0)", true, "Entity: world handle" );
	CHECK_LUA_INT( "Entity(0):EntIndex()", 0, "Entity: world index 0" );
	CHECK_LUA_BOOL( "IsValid(Entity(1))", true, "Entity: live player valid" );
	CHECK_LUA_INT( "Entity(1):EntIndex()", 1, "Entity: player index" );
}

// Pins: out-of-range / non-integer / non-finite indices yield nil WITHOUT
// touching the engine INDEXENT callback.
TEST(Spatial_entity_invalid_nil)
{
	LUA( "Before = indexcalls()" );
	CHECK_LUA_NIL( "Entity(-1)", "Entity: negative index nil" );
	CHECK_LUA_NIL( "Entity(16)", "Entity: index == maxEntities nil" );
	CHECK_LUA_NIL( "Entity(0.5)", "Entity: fractional index nil" );
	CHECK_LUA_NIL( "Entity(-0.1)", "Entity: negative fractional nil" );
	CHECK_LUA_NIL( "Entity(1/0)", "Entity: +inf nil" );
	CHECK_LUA_NIL( "Entity(0/0)", "Entity: NaN nil" );
	CHECK_LUA_INT( "indexcalls() - Before", 0, "Entity: invalid indices never call INDEXENT" );
}

// Pins: a non-number argument to Entity raises (checknumber), not nil.
TEST(Spatial_entity_typecheck)
{
	CHECK_LUA_ERROR( "return Entity('x')", "number expected", "Entity: string argument raises" );
	CHECK_LUA_ERROR( "return Entity()", "number expected", "Entity: missing argument raises" );
}

// Pins: freed slot returns nil from Entity (PushEntity nils free edicts).
TEST(Spatial_entity_freed_nil)
{
	LUA( "invalidate(6)" );
	CHECK_LUA_NIL( "Entity(6)", "Entity: freed slot nil" );
	LUA( "reuse(6)" );
	CHECK_LUA_BOOL( "IsValid(Entity(6))", true, "Entity: reused slot valid again" );
}

// Pins: GetGroundEntity returns the ground handle, nil for null/free ground.
TEST(Spatial_ground_entity)
{
	LUA( "setground(1, 0)" );
	CHECK_LUA_BOOL( "Entity(1):GetGroundEntity() == Entity(0)", true, "GetGroundEntity: world ground identity" );
	LUA( "setground(1, nil)" );
	CHECK_LUA_NIL( "Entity(1):GetGroundEntity()", "GetGroundEntity: null ground nil" );
	LUA( "setground(1, 6); invalidate(6)" );
	CHECK_LUA_NIL( "Entity(1):GetGroundEntity()", "GetGroundEntity: freed ground nil" );
}

// Pins: SetPos writes origin through UTIL_SetOrigin exactly once.
TEST(Spatial_setpos_writethrough)
{
	LUA( "local p = Entity(1); p:SetPos(Vector(1,2,3))" );
	CHECK_LUA_FLT( "slot(1):GetFeetPos().x", 1, "SetPos: origin.x written (feet x == origin x)" );
	CHECK_LUA_INT( "select(3, engineCounts())", 1, "SetPos: one UTIL_SetOrigin call" );
	CHECK_LUA_INT( "select(1, lastEngineEnts())", 1, "SetPos: UTIL_SetOrigin targeted the right edict" );
}

// Pins: SetPos rejects non-finite vectors and non-Vector args, no engine call.
TEST(Spatial_setpos_validation)
{
	CHECK_LUA_ERROR( "slot(1):SetPos(Vector(0/0,2,3))", "position must be finite", "SetPos: NaN rejected" );
	CHECK_LUA_ERROR( "slot(1):SetPos(Vector(1/0,2,3))", "position must be finite", "SetPos: inf rejected" );
	CHECK_LUA_ERROR( "slot(1):SetPos({1,2,3})", "XGLua_Vector expected", "SetPos: plain table rejected" );
	CHECK_LUA_INT( "select(3, engineCounts())", 0, "SetPos: rejected calls never reach UTIL_SetOrigin" );
}

// Pins: SetCollisionBounds writes mins/maxs and derives size.
TEST(Spatial_setbounds_writethrough)
{
	LUA( "slot(1):SetCollisionBounds(Vector(-1,-2,-3), Vector(4,5,6))" );
	CHECK_LUA_FLT( "slot(1):OBBMins().x", -1, "SetCollisionBounds: mins written" );
	CHECK_LUA_FLT( "slot(1):OBBMaxs().y", 5, "SetCollisionBounds: maxs written" );
	CHECK_LUA_FLT( "({slot(1):GetCollisionBounds()})[2].z", 6, "SetCollisionBounds: maxs.z via GetCollisionBounds" );
	CHECK_LUA_INT( "select(4, engineCounts())", 1, "SetCollisionBounds: one UTIL_SetSize call" );
	CHECK_LUA_INT( "select(2, lastEngineEnts())", 1, "SetCollisionBounds: UTIL_SetSize targeted the right edict" );
}

// Pins: SetCollisionBounds rejects non-finite and unordered bounds.
TEST(Spatial_setbounds_validation)
{
	CHECK_LUA_ERROR( "slot(1):SetCollisionBounds(Vector(0,0,0), Vector(1,1,1/0))", "collision bounds must be finite", "SetCollisionBounds: inf maxs rejected" );
	CHECK_LUA_ERROR( "slot(1):SetCollisionBounds(Vector(4,0,0), Vector(3,1,1))", "collision mins must not exceed maxs", "SetCollisionBounds: unordered rejected" );
	CHECK_LUA_INT( "select(4, engineCounts())", 0, "SetCollisionBounds: rejected calls never reach UTIL_SetSize" );
	LUA( "slot(1):SetCollisionBounds(Vector(2,2,2), Vector(2,2,2))" );
	CHECK_LUA_INT( "select(4, engineCounts())", 1, "SetCollisionBounds: degenerate equal bounds accepted" );
}

// Pins: ents.GetAll returns live initialized entities including world, dense.
TEST(Spatial_getall)
{
	LUA( "invalidate(6)" );
	LUA( "All = ents.GetAll()" );
	CHECK_LUA_INT( "#All", 6, "ents.GetAll: world + 5 live entities" );
	CHECK_LUA_BOOL( "All[1] == Entity(0)", true, "ents.GetAll: world first" );
	CHECK_LUA_BOOL( "All[2] == Entity(1)", true, "ents.GetAll: slot order preserved" );
	LUA( "setpriv(3, false)" );
	CHECK_LUA_INT( "#ents.GetAll()", 5, "ents.GetAll: uninitialized private data skipped" );
}

// Pins: ents.FindByClass exact and trailing-wildcard prefix matching.
TEST(Spatial_findbyclass)
{
	CHECK_LUA_INT( "#ents.FindByClass('player')", 2, "ents.FindByClass: exact match both players" );
	CHECK_LUA_INT( "#ents.FindByClass('worldspawn')", 1, "ents.FindByClass: world class" );
	CHECK_LUA_INT( "#ents.FindByClass('weapon_*')", 2, "ents.FindByClass: wildcard prefix" );
	CHECK_LUA_INT( "#ents.FindByClass('weapon_lua_test')", 1, "ents.FindByClass: exact lua weapon" );
	CHECK_LUA_INT( "#ents.FindByClass('nosuch')", 0, "ents.FindByClass: no match empty" );
	CHECK_LUA_INT( "#ents.FindByClass(42)", 0, "ents.FindByClass: number coerced to string, no match" );
	CHECK_LUA_ERROR( "return ents.FindByClass({})", "string expected", "ents.FindByClass: table argument raises" );
}

// Pins: Spawn/Remove dispatch once for non-world; world is guarded.
TEST(Spatial_spawn_remove)
{
	LUA( "local p = Entity(1); p:Spawn(); p:Remove()" );
	CHECK_LUA_INT( "select(1, engineCounts())", 1, "Spawn: one DispatchSpawn" );
	CHECK_LUA_INT( "select(2, engineCounts())", 1, "Remove: one SUB_Remove" );
	CHECK_LUA_INT( "select(3, lastEngineEnts())", 1, "Spawn: targeted the player edict" );
	CHECK_LUA_ERROR( "slot(0):Spawn()", "Cannot spawn the world entity!", "Spawn: world guarded" );
	CHECK_LUA_ERROR( "slot(0):Remove()", "Cannot remove the world entity!", "Remove: world guarded" );
}

// Pins: GetMaxHealth reads pev even when private data is absent.
TEST(Spatial_maxhealth_no_private)
{
	LUA( "setmaxhealth(0, 42); setpriv(0, false)" );
	CHECK_LUA_FLT( "slot(0):GetMaxHealth()", 42, "GetMaxHealth: works without private data" );
	LUA( "setpriv(0, true)" );
}

// Pins: stale handles raise on every spatial method.
TEST(Spatial_stale_errors)
{
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "Stale:SetPos(Vector(1,2,3))", "Tried to use a NULL entity!", "stale: SetPos raises" );
	CHECK_LUA_ERROR( "Stale:SetCollisionBounds(Vector(0,0,0),Vector(1,1,1))", "Tried to use a NULL entity!", "stale: SetCollisionBounds raises" );
	CHECK_LUA_ERROR( "return Stale:GetGroundEntity()", "Tried to use a NULL entity!", "stale: GetGroundEntity raises" );
	CHECK_LUA_NIL( "slot(1)", "stale: fresh acquisition of a freed slot is nil" );
	CHECK_LUA_INT( "select(3, engineCounts())", 0, "stale: no engine write-through" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_spatial" );
}