// Regression for the read-only geometry bindings
// (dlls/lua/xglua_entity_geometry.h): GetFeetPos, WorldSpaceCenter,
// OBBMins/OBBMaxs/GetCollisionBounds, ShootPos, EyeAngles (Angle contract),
// GetAimVector (private basis, no gpGlobals clobber, no punchangle).

#include "entity_fixture.h"

// Pins: GetFeetPos keeps origin x/y and drops z to origin.z + mins.z.
TEST(Geometry_feetpos)
{
	LUA( "setorigin(1, 10, 20, 30); setmins(1, -16, -16, -36)" );
	LUA( "F = slot(1):GetFeetPos()" );
	CHECK_LUA_FLT( "F.x", 10, "GetFeetPos: x stays origin.x" );
	CHECK_LUA_FLT( "F.y", 20, "GetFeetPos: y stays origin.y" );
	CHECK_LUA_FLT( "F.z", -6, "GetFeetPos: z = origin.z + mins.z (30 - 36)" );
	LUA( "setmins(1, -16, -16, -18)" );
	CHECK_LUA_FLT( "slot(1):GetFeetPos().z", 12, "GetFeetPos: crouch hull changes feet z" );
}

// Pins: WorldSpaceCenter is the midpoint of the ENGINE abs bounds, not
// origin plus local bounds.
TEST(Geometry_worldspacecenter)
{
	LUA( "setorigin(1, 100, 100, 100); setabs(1, 0, 0, 0, 10, 20, 30)" );
	LUA( "C = slot(1):WorldSpaceCenter()" );
	CHECK_LUA_FLT( "C.x", 5, "WorldSpaceCenter: abs midpoint x" );
	CHECK_LUA_FLT( "C.y", 10, "WorldSpaceCenter: abs midpoint y" );
	CHECK_LUA_FLT( "C.z", 15, "WorldSpaceCenter: abs midpoint z" );
	CHECK_LUA_BOOL( "C.x == 100", false, "WorldSpaceCenter: ignores origin" );
}

// Pins: OBB accessors return copies of local mins/maxs; GetCollisionBounds
// returns both in mins,maxs order; results are detached from pev.
TEST(Geometry_obb_copies)
{
	LUA( "setmins(1, -16, -16, -36); setmaxs(1, 16, 16, 36)" );
	CHECK_LUA_FLT( "slot(1):OBBMins().z", -36, "OBBMins: local mins copy" );
	CHECK_LUA_FLT( "slot(1):OBBMaxs().z", 36, "OBBMaxs: local maxs copy" );
	LUA( "Mi, Ma = slot(1):GetCollisionBounds()" );
	CHECK_LUA_FLT( "Mi.z", -36, "GetCollisionBounds: first result is mins" );
	CHECK_LUA_FLT( "Ma.z", 36, "GetCollisionBounds: second result is maxs" );
	LUA( "Mi.z = 999" );
	CHECK_LUA_FLT( "slot(1):OBBMins().z", -36, "GetCollisionBounds: result is a detached copy" );
}

// Pins: ShootPos is the native gun position (origin + view_ofs).
TEST(Geometry_shootpos)
{
	LUA( "setorigin(1, 1, 2, 3); setviewofs(1, 0, 0, 28)" );
	LUA( "S = slot(1):ShootPos()" );
	CHECK_LUA_FLT( "S.x", 1, "ShootPos: x" );
	CHECK_LUA_FLT( "S.y", 2, "ShootPos: y" );
	CHECK_LUA_FLT( "S.z", 31, "ShootPos: z = origin.z + view_ofs.z" );
	CHECK_LUA_ERROR( "return slot(3):ShootPos()", "non-player entity as a Player", "ShootPos: non-player raises" );
}

// Pins: EyeAngles returns an Angle (p/y/r), a copy of v_angle without
// punchangle.
TEST(Geometry_eyeangles)
{
	LUA( "setvangle(1, 10, 20, 30); setpunch(1, 5, 6, 7)" );
	LUA( "A = slot(1):EyeAngles()" );
	CHECK_LUA_FLT( "A.p", 10, "EyeAngles: pitch from v_angle.x" );
	CHECK_LUA_FLT( "A.y", 20, "EyeAngles: yaw from v_angle.y" );
	CHECK_LUA_FLT( "A.r", 30, "EyeAngles: roll from v_angle.z" );
	CHECK_LUA_FLT( "A.pitch", 10, "EyeAngles: pitch alias (Angle type)" );
	CHECK_LUA_NIL( "A.x", "EyeAngles: Angle has no Vector x field" );
	LUA( "A.p = 999" );
	CHECK_LUA_FLT( "slot(1):EyeAngles().p", 10, "EyeAngles: result is a detached copy" );
	CHECK_LUA_ERROR( "return slot(3):EyeAngles()", "non-player entity as a Player", "EyeAngles: non-player raises" );
}

// Pins: GetAimVector uses v_angle through the private helper, adds no
// punchangle, and never overwrites gpGlobals' direction vectors.
TEST(Geometry_aimvector)
{
	LUA( "setvangle(1, 10, 20, 30); setpunch(1, 5, 6, 7)" );
	LUA( "D = slot(1):GetAimVector()" );
	CHECK_LUA_FLT( "D.x", 0.5, "GetAimVector: forward from the private basis" );
	CHECK_LUA_FLT( "D.y", 0.25, "GetAimVector: forward y" );
	CHECK_LUA_FLT( "D.z", 0.125, "GetAimVector: forward z" );
	CHECK_LUA_INT( "select(1, aimInfo())", 1, "GetAimVector: one MakeVectorsPrivate call" );
	CHECK_LUA_FLT( "select(2, aimInfo())", 10, "GetAimVector: passed v_angle.x (no punchangle)" );
	CHECK_LUA_FLT( "select(3, aimInfo())", 20, "GetAimVector: passed v_angle.y" );
	CHECK_EQ_FLT( globals.v_forward.x, 9, "GetAimVector: gpGlobals.v_forward untouched" );
	CHECK_EQ_FLT( globals.v_right.y, 8, "GetAimVector: gpGlobals.v_right untouched" );
	CHECK_EQ_FLT( globals.v_up.z, 7, "GetAimVector: gpGlobals.v_up untouched" );
}

// Pins: geometry methods raise on stale handles.
TEST(Geometry_stale_errors)
{
	LUA( "Stale = Entity(1); invalidate(1)" );
	CHECK_LUA_ERROR( "return Stale:GetFeetPos()", "Tried to use a NULL entity!", "stale: GetFeetPos raises" );
	CHECK_LUA_ERROR( "return Stale:WorldSpaceCenter()", "Tried to use a NULL entity!", "stale: WorldSpaceCenter raises" );
	CHECK_LUA_ERROR( "return Stale:OBBMins()", "Tried to use a NULL entity!", "stale: OBBMins raises" );
	CHECK_LUA_ERROR( "return Stale:GetAimVector()", "Tried to use a NULL entity!", "stale: GetAimVector raises" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_geometry" );
}