// Regression for the REAL Vector / Angle userdata bindings
// (dlls/lua/xglua_types.cpp, compiled directly into this test).
// Only Vector itself is stubbed; every method, metamethod and the
// registration function XGLua_RegisterTypes are production code.

#define XGLUA_H
struct Vector
{
	float x, y, z;
	Vector( float a = 0, float b = 0, float c = 0 ) : x( a ), y( b ), z( c ) {}
};
#include "test_framework.h"
#include "xglua_types.cpp"

static bool SetupTypes( lua_State *L )
{
	XGLua_RegisterTypes( L );
	return true;
}

// Pins: Vector() defaults to the zero vector and stores the given floats.
TEST(Vector_construct)
{
	CHECK_LUA_FLT( "Vector().x", 0, "Vector: default x" );
	CHECK_LUA_FLT( "Vector().y", 0, "Vector: default y" );
	CHECK_LUA_FLT( "Vector().z", 0, "Vector: default z" );
	CHECK_LUA_FLT( "Vector(1,2,3).x", 1, "Vector: stored x" );
	CHECK_LUA_FLT( "Vector(1,2,3).y", 2, "Vector: stored y" );
	CHECK_LUA_FLT( "Vector(1,2,3).z", 3, "Vector: stored z" );
	CHECK_LUA_TYPE( "Vector(1,2,3)", "userdata", "Vector: userdata type" );
}

// Pins: x/y/z reads and writes, uppercase aliases, invalid key rejection.
TEST(Vector_fields)
{
	LUA( "V = Vector(1,2,3)" );
	CHECK_LUA_FLT( "V.X", 1, "Vector: uppercase X alias" );
	CHECK_LUA_FLT( "V.Y", 2, "Vector: uppercase Y alias" );
	CHECK_LUA_FLT( "V.Z", 3, "Vector: uppercase Z alias" );
	LUA( "V.x = 10; V.Y = 20" );
	CHECK_LUA_FLT( "V.x", 10, "Vector: lowercase write" );
	CHECK_LUA_FLT( "V.y", 20, "Vector: uppercase write" );
	CHECK_LUA_ERROR( "V.w = 1", "invalid Vector key", "Vector: unknown field write raises" );
	CHECK_LUA_NIL( "V.nope", "Vector: unknown field read is nil (metatable fallthrough)" );
}

// Pins: Length / Length2D / LengthSqr on the 3-4-12 triangle.
TEST(Vector_lengths)
{
	LUA( "V = Vector(3,4,12)" );
	CHECK_LUA_FLT( "V:Length()", 13, "Vector:Length 3-4-12" );
	CHECK_LUA_FLT( "V:Length2D()", 5, "Vector:Length2D ignores z" );
	CHECK_LUA_FLT( "V:LengthSqr()", 169, "Vector:LengthSqr" );
	CHECK_LUA_FLT( "Vector():Length()", 0, "Vector:Length of zero" );
}

// Pins: Normalize mutates in place and returns the length; GetNormalized
// returns a fresh vector; near-zero vectors are not divided.
TEST(Vector_normalize)
{
	LUA( "V = Vector(0,3,4)" );
	CHECK_LUA_FLT( "V:Normalize()", 5, "Vector:Normalize returns old length" );
	CHECK_LUA_FLT( "V.y", 0.6, "Vector:Normalize mutated y" );
	CHECK_LUA_FLT( "V.z", 0.8, "Vector:Normalize mutated z" );
	LUA( "W = Vector(0,3,4); N = W:GetNormalized()" );
	CHECK_LUA_FLT( "N.y", 0.6, "Vector:GetNormalized result" );
	CHECK_LUA_FLT( "W.y", 3, "Vector:GetNormalized leaves source untouched" );
	CHECK_LUA_BOOL( "N == W", false, "Vector:GetNormalized is a distinct object" );
	LUA( "T = Vector(0.00001,0,0)" );
	CHECK_LUA_FLT( "T:GetNormalized().x", 0, "Vector:GetNormalized of near-zero is zero" );
	CHECK_LUA_FLT( "T.x", 0.00001, "Vector:Normalize of near-zero keeps components" );
}

// Pins: Distance, Dot and Cross arithmetic.
TEST(Vector_distance_dot_cross)
{
	CHECK_LUA_FLT( "Vector(0,0,0):Distance(Vector(3,4,0))", 5, "Vector:Distance" );
	CHECK_LUA_FLT( "Vector(1,2,3):Dot(Vector(4,5,6))", 32, "Vector:Dot" );
	LUA( "C = Vector(1,0,0):Cross(Vector(0,1,0))" );
	CHECK_LUA_FLT( "C.x", 0, "Vector:Cross x" );
	CHECK_LUA_FLT( "C.y", 0, "Vector:Cross y" );
	CHECK_LUA_FLT( "C.z", 1, "Vector:Cross z" );
}

// Pins: Vector:Angle() produces an Angle with GMod pitch/yaw semantics.
TEST(Vector_to_angle)
{
	LUA( "A = Vector(1,0,0):Angle()" );
	CHECK_LUA_FLT( "A.p", 0, "Vector:Angle +X pitch" );
	CHECK_LUA_FLT( "A.y", 0, "Vector:Angle +X yaw" );
	CHECK_LUA_FLT( "Vector(0,1,0):Angle().y", 90, "Vector:Angle +Y yaw 90" );
	CHECK_LUA_FLT( "Vector(0,0,1):Angle().p", -90, "Vector:Angle +Z pitch -90" );
	CHECK_LUA_FLT( "Vector():Angle().p", 0, "Vector:Angle of zero vector" );
	CHECK_LUA_FLT( "Vector():Angle().y", 0, "Vector:Angle of zero vector yaw" );
}

// Pins: __add/__sub/__mul/__unm produce new vectors, sources untouched.
TEST(Vector_arithmetic)
{
	LUA( "A = Vector(1,2,3); B = Vector(10,20,30)" );
	LUA( "S = A + B; D = B - A; M = A * 2; U = -A" );
	CHECK_LUA_FLT( "S.z", 33, "Vector:__add" );
	CHECK_LUA_FLT( "D.z", 27, "Vector:__sub" );
	CHECK_LUA_FLT( "M.z", 6, "Vector:__mul scalar" );
	CHECK_LUA_FLT( "U.z", -3, "Vector:__unm" );
	CHECK_LUA_FLT( "A.z", 3, "Vector: arithmetic leaves operands untouched" );
	CHECK_LUA_ERROR( "return A + 5", "XGLua_Vector expected", "Vector:__add with non-vector raises" );
}

// Pins: value-based __eq, non-userdata comparison is false, __tostring
// format, IsZero.
TEST(Vector_eq_tostring_iszero)
{
	CHECK_LUA_BOOL( "Vector(1,2,3) == Vector(1,2,3)", true, "Vector:__eq value equality" );
	CHECK_LUA_BOOL( "Vector(1,2,3) == Vector(1,2,4)", false, "Vector:__eq differing values" );
	CHECK_LUA_BOOL( "Vector(1,2,3) == 5", false, "Vector:__eq against number is false" );
	CHECK_LUA_BOOL( "Vector(1,2,3) == nil", false, "Vector:__eq against nil is false" );
	CHECK_LUA_STR( "tostring(Vector(1.5,2,3))", "1.50 2.00 3.00", "Vector:__tostring format" );
	CHECK_LUA_BOOL( "Vector():IsZero()", true, "Vector:IsZero true" );
	CHECK_LUA_BOOL( "Vector(0,0,0.1):IsZero()", false, "Vector:IsZero false" );
}

// Pins: comparing a Vector with an Angle userdata is false without calling
// Vec_Eq (LuaJIT only dispatches __eq for matching metatables).
TEST(Vector_eq_angle_cross_type)
{
	CHECK_LUA_BOOL( "Vector(1,2,3) == Angle(1,2,3)", false, "Vector:__eq vs Angle is false, no raise" );
	CHECK_LUA_BOOL( "Angle(1,2,3) == Vector(1,2,3)", false, "Angle vs Vector is false, no raise" );
}

// Pins: Angle construction, p/y/r and pitch/yaw/roll aliases.
TEST(Angle_fields)
{
	LUA( "A = Angle(10,20,30)" );
	CHECK_LUA_FLT( "A.p", 10, "Angle:p" );
	CHECK_LUA_FLT( "A.y", 20, "Angle:y" );
	CHECK_LUA_FLT( "A.r", 30, "Angle:r" );
	CHECK_LUA_FLT( "A.pitch", 10, "Angle:pitch alias" );
	CHECK_LUA_FLT( "A.yaw", 20, "Angle:yaw alias" );
	CHECK_LUA_FLT( "A.roll", 30, "Angle:roll alias" );
	CHECK_LUA_FLT( "Angle().p", 0, "Angle: default zero" );
	CHECK_LUA_NIL( "A.x", "Angle: no Vector-style x field" );
}

// Pins: Angle field writes through both alias sets, Zero(), invalid keys.
TEST(Angle_writes_zero)
{
	LUA( "A = Angle(1,2,3)" );
	LUA( "A.pitch = 11; A.y = 22; A.roll = 33" );
	CHECK_LUA_FLT( "A.p", 11, "Angle: write via pitch" );
	CHECK_LUA_FLT( "A.y", 22, "Angle: write via y" );
	CHECK_LUA_FLT( "A.r", 33, "Angle: write via roll" );
	LUA( "A:Zero()" );
	CHECK_LUA_FLT( "A.p + A.y + A.r", 0, "Angle:Zero clears all components" );
	CHECK_LUA_ERROR( "A.q = 1", "invalid Angle key", "Angle: unknown field write raises" );
	CHECK_LUA_STR( "tostring(Angle(1,2,3))", "1.00 2.00 3.00", "Angle:__tostring format" );
}

// Pins: method self-type enforcement — Vector methods reject Angle self
// and plain tables; Angle methods reject Vector self.
TEST(Type_self_validation)
{
	CHECK_LUA_ERROR( "return Vector(1,2,3).Length( Angle(1,2,3) )", "XGLua_Vector expected",
		"Vector:Length rejects Angle self" );
	CHECK_LUA_ERROR( "return Vector(1,2,3).Length( {} )", "XGLua_Vector expected",
		"Vector:Length rejects table self" );
	CHECK_LUA_ERROR( "return Angle(1,2,3).Zero( Vector(1,2,3) )", "XGLua_Angle expected",
		"Angle:Zero rejects Vector self" );
	CHECK_LUA_ERROR( "return Vector(1,2,3):Distance( {} )", "XGLua_Vector expected",
		"Vector:Distance validates second argument" );
}

int main()
{
	xtest::Setup() = SetupTypes;
	return xtest::RunAll( "vector_types" );
}