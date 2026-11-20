// Regression for util.TraceLine / util.TraceHull
// (dlls/lua/xglua_util_trace.h): strict option whitelist, finite/coordinate
// guards, engine flag/hull/filter wiring and the result-table semantics
// (hit detection, world vs entity, solid suppression of the hit plane).

#include "entity_fixture.h"

// Pins: a mid-trace entity hit maps every result field.
TEST(Trace_line_hit)
{
	LUA( "traceReset(); traceCfg(0.5, 3, 0,0, 0,1, 2, 0,1,0)" );
	LUA( "R = util.TraceLine{ start = Vector(1,2,3), endpos = Vector(11,2,3) }" );
	CHECK_LUA_BOOL( "R.Hit", true, "TraceLine: fraction < 1 is a hit" );
	CHECK_LUA_BOOL( "R.HitNonWorld", true, "TraceLine: non-world hit" );
	CHECK_LUA_BOOL( "R.HitWorld", false, "TraceLine: not world" );
	CHECK_LUA_BOOL( "R.Entity == Entity(2)", true, "TraceLine: hit entity handle" );
	CHECK_LUA_FLT( "R.Fraction", 0.5, "TraceLine: engine fraction" );
	CHECK_LUA_INT( "R.HitGroup", 3, "TraceLine: engine hitgroup" );
	CHECK_LUA_FLT( "R.HitPos.x", 11, "TraceLine: HitPos is the engine end position" );
	CHECK_LUA_FLT( "R.HitNormal.y", 1, "TraceLine: HitNormal copy" );
	CHECK_LUA_BOOL( "R.InWater", true, "TraceLine: InWater passthrough" );
	CHECK_LUA_BOOL( "R.InOpen", false, "TraceLine: InOpen passthrough" );
	LUA( "local calls, flags, hull, skip = traceInfo()" );
	CHECK_LUA_INT( "select(1, traceInfo())", 1, "TraceLine: one engine call" );
	CHECK_LUA_INT( "select(2, traceInfo())", 0, "TraceLine: default flags 0" );
	CHECK_LUA_INT( "select(4, traceInfo())", -1, "TraceLine: no skip entity by default" );
}

// Pins: a miss reports no entity even if the engine returns world pHit.
TEST(Trace_line_miss)
{
	LUA( "traceCfg(1, 9, 0,0, 0,0, 0, 0,0,1)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_BOOL( "R.Hit", false, "TraceLine: fraction 1 is a miss" );
	CHECK_LUA_NIL( "R.Entity", "TraceLine: miss never reports world pHit as entity" );
	CHECK_LUA_BOOL( "R.HitWorld", false, "TraceLine: miss HitWorld false" );
	CHECK_LUA_BOOL( "R.HitNonWorld", false, "TraceLine: miss HitNonWorld false" );
	CHECK_LUA_INT( "R.HitGroup", 0, "TraceLine: miss zeroes HitGroup" );
	CHECK_LUA_FLT( "R.HitNormal.x + R.HitNormal.y + R.HitNormal.z", 0, "TraceLine: miss zeroes HitNormal" );
	CHECK_LUA_FLT( "R.Fraction", 1, "TraceLine: full fraction" );
	CHECK_LUA_FLT( "R.HitPos.x", 5, "TraceLine: miss HitPos is the requested end" );
}

// Pins: world hit maps to Entity(0) with HitWorld.
TEST(Trace_line_world)
{
	LUA( "traceCfg(0.5, 0, 0,0, 0,0, 0, 0,0,1)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_BOOL( "R.HitWorld", true, "TraceLine: world hit flag" );
	CHECK_LUA_BOOL( "R.HitNonWorld", false, "TraceLine: world is not non-world" );
	CHECK_LUA_BOOL( "R.Entity == Entity(0)", true, "TraceLine: world hit entity is Entity(0)" );
}

// Pins: StartSolid/AllSolid count as hits but suppress the hit plane.
TEST(Trace_line_solid)
{
	LUA( "traceCfg(1, 0, 1,0, 1,0, 2, 0,1,0)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_BOOL( "R.Hit", true, "TraceLine: StartSolid is a hit at fraction 1" );
	CHECK_LUA_BOOL( "R.StartSolid", true, "TraceLine: StartSolid passthrough" );
	CHECK_LUA_FLT( "R.HitNormal.y", 0, "TraceLine: StartSolid suppresses the plane" );
	CHECK_LUA_BOOL( "R.Entity == Entity(2)", true, "TraceLine: StartSolid still reports pHit" );
	LUA( "traceCfg(1, 0, 0,1, 0,0, 2, 0,1,1)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_BOOL( "R.AllSolid", true, "TraceLine: AllSolid passthrough" );
	CHECK_LUA_FLT( "R.HitNormal.z", 0, "TraceLine: AllSolid suppresses the plane" );
}

// Pins: freed pHit yields a hit without an entity handle.
TEST(Trace_line_freed_phit)
{
	LUA( "traceCfg(0.5, 0, 0,0, 0,0, 2, 0,1,0); invalidate(2)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_BOOL( "R.Hit", true, "TraceLine: freed pHit still a hit" );
	CHECK_LUA_NIL( "R.Entity", "TraceLine: freed pHit maps to nil entity" );
	LUA( "reuse(2)" );
}

// Pins: ignoremonsters and filter wire into the engine call.
TEST(Trace_line_options)
{
	LUA( "traceReset(); traceCfg(0.5, 0, 0,0, 0,0, 2, 0,1,0)" );
	LUA( "util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), ignoremonsters = true }" );
	CHECK_LUA_INT( "select(2, traceInfo())", 1, "TraceLine: ignoremonsters true -> flags 1" );
	LUA( "util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), ignoremonsters = false, filter = Entity(2) }" );
	CHECK_LUA_INT( "select(2, traceInfo())", 0, "TraceLine: ignoremonsters false -> flags 0" );
	CHECK_LUA_INT( "select(4, traceInfo())", 2, "TraceLine: filter passed as the skip edict" );
	LUA( "util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), filter = nil }" );
	CHECK_LUA_INT( "select(4, traceInfo())", -1, "TraceLine: nil filter -> no skip" );
	CHECK_EQ_FLT( lastTraceStart.x, 0, "TraceLine: start passed to the engine" );
	CHECK_EQ_FLT( lastTraceEnd.x, 5, "TraceLine: endpos passed to the engine" );
}

// Pins: options are read raw — a throwing __index is never consulted.
TEST(Trace_line_raw_options)
{
	LUA( "traceCfg(0.5, 0, 0,0, 0,0, -1, 0,0,0)\n"
		"Opts = setmetatable( { start = Vector(0,0,0), endpos = Vector(5,0,0) },\n"
		"	{ __index = function() error( 'metamethod must not run' ) end } )\n"
		"R = util.TraceLine( Opts )" );
	CHECK_LUA_FLT( "R.Fraction", 0.5, "TraceLine: raw option reads bypass __index" );
}

// Pins: the strict option whitelist and vector guards.
TEST(Trace_line_validation)
{
	LUA( "traceCfg(0.5, 0, 0,0, 0,0, 2, 0,1,0)" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), bogus = 1 }", "unsupported trace option 'bogus'", "TraceLine: unknown option rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 1 }", "unsupported trace option 'hull'", "TraceLine: hull key rejected on line trace" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), [1] = 1 }", "trace options must use named fields", "TraceLine: non-string key rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), ['bad\\0key'] = 1 }", "NUL bytes", "TraceLine: NUL in key rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0/0,0,0), endpos = Vector(5,0,0) }", "trace start must be a finite Vector", "TraceLine: NaN start rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(1/0,0,0) }", "trace endpos must be a finite Vector", "TraceLine: inf endpos rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(3e8,0,0) }", "exceeds the coordinate safety limit", "TraceLine: beyond coordinate limit rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = 'nope' }", "XGLua_Vector expected", "TraceLine: non-vector endpos rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0) }", "XGLua_Vector expected", "TraceLine: missing endpos rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine('notatable')", "table expected", "TraceLine: non-table options rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), ignoremonsters = 1 }", "ignoremonsters must be a boolean", "TraceLine: non-boolean ignoremonsters rejected" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(1e8,0,0) }" );
	CHECK_LUA_FLT( "R.Fraction", 0.5, "TraceLine: 1e8 inside the coordinate limit accepted" );
}

// Pins: filter must be a valid entity handle.
TEST(Trace_line_filter_validation)
{
	LUA( "traceCfg(0.5, 0, 0,0, 0,0, 2, 0,1,0); Stale = Entity(2); invalidate(2)" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), filter = Stale }", "Tried to use a NULL entity!", "TraceLine: stale filter rejected" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0), filter = {} }", "XGLua_Entity expected", "TraceLine: table filter rejected" );
	LUA( "reuse(2)" );
}

// Pins: TraceHull passes the hull number and validates 0..3 integers.
TEST(Trace_hull)
{
	LUA( "traceReset(); traceCfg(0.5, 0, 0,0, 0,0, 2, 0,1,0)" );
	LUA( "R = util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 2 }" );
	CHECK_LUA_BOOL( "R.Hit", true, "TraceHull: result mapping shared with line" );
	CHECK_LUA_INT( "select(3, traceInfo())", 2, "TraceHull: hull number passed to the engine" );
	LUA( "util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 0, ignoremonsters = true }" );
	CHECK_LUA_INT( "select(3, traceInfo())", 0, "TraceHull: HULL_POINT accepted" );
	CHECK_LUA_INT( "select(2, traceInfo())", 1, "TraceHull: ignoremonsters wired" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0) }", "TraceHull requires a numeric HULL_* constant", "TraceHull: hull required" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 'x' }", "TraceHull requires a numeric HULL_* constant", "TraceHull: string hull rejected" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = -1 }", "hull must be an integer from 0 to 3", "TraceHull: negative hull rejected" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 4 }", "hull must be an integer from 0 to 3", "TraceHull: hull above 3 rejected" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 1.5 }", "hull must be an integer from 0 to 3", "TraceHull: fractional hull rejected" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 0/0 }", "hull must be an integer from 0 to 3", "TraceHull: NaN hull rejected" );
}

// Pins: missing engine trace APIs raise explicit errors, recovery works.
TEST(Trace_engine_unavailable)
{
	LUA( "engineTrace(false)" );
	CHECK_LUA_ERROR( "return util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }", "TraceLine engine API is unavailable", "TraceLine: unavailable engine error" );
	CHECK_LUA_ERROR( "return util.TraceHull{ start = Vector(0,0,0), endpos = Vector(5,0,0), hull = 1 }", "TraceHull engine API is unavailable", "TraceHull: unavailable engine error" );
	LUA( "engineTrace(true); traceCfg(0.5, 0, 0,0, 0,0, -1, 0,0,0)" );
	LUA( "R = util.TraceLine{ start = Vector(0,0,0), endpos = Vector(5,0,0) }" );
	CHECK_LUA_FLT( "R.Fraction", 0.5, "TraceLine: works again after the engine returns" );
}

int main()
{
	xtest::Setup() = SetupEntityFixture;
	return xtest::RunAll( "entity_trace" );
}