#include <cmath>
#include <cstdio>
#include "../../dlls/lua/xglua_attack_time.h"

static int failures = 0;
static void check( bool ok, const char *message )
{
    if( !ok ) { std::printf( "FAIL: %s\n", message ); ++failures; }
}
static bool near( float a, float b ) { return std::fabs( a - b ) < 0.0001f; }

int main()
{
    // Same 67 ms delay represented by the native prediction and world clocks.
    check( near( XGLua_AttackDeadline( 100.0f, 0.067f, 0.0f ), 100.067f ), "prediction countdown becomes a world deadline" );
    check( near( XGLua_AttackDeadline( 100.0f, 100.067f, 100.0f ), 100.067f ), "absolute native time stays absolute" );
    check( near( XGLua_AttackDeadline( 100.0f, -0.001f, 0.0f ), 100.1f ), "late countdown uses original fallback" );
    check( near( XGLua_AttackDeadline( 100.0f, 99.999f, 100.0f ), 100.1f ), "late absolute time uses original fallback" );
    check( near( XGLua_AttackDeadline( 100.0f, 0.0f, 0.0f ), 100.0f ), "zero remaining is not late" );

    // At 1000 frames/s, one 67ms countdown must block the next 66 frames.
    const float deadline = XGLua_AttackDeadline( 10.0f, 0.067f, 0.0f );
    int allowed = 0;
    for( int frame = 1; frame <= 66; ++frame )
        if( 10.0f + frame * 0.001f >= deadline ) ++allowed;
    check( allowed == 0, "a held trigger cannot fire every frame" );
    check( 10.068f >= deadline, "a held trigger can fire after deadline" );
    if( failures ) return 1;
    std::puts( "attack deadline: 7 checks passed" );
    return 0;
}
