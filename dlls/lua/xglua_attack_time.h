#ifndef XGLUA_ATTACK_TIME_H
#define XGLUA_ATTACK_TIME_H

// Native GetNextAttackDelay uses UTIL_WeaponTimeBase: zero for a
// predicted weapon or the current world time for an unpredicted weapon.
// Lua SWEP deadlines always use the world clock.
inline float XGLua_AttackDeadline( float now, float nativeTime, float nativeBase )
{
    const float deadline = now + ( nativeTime - nativeBase );
    // Match the original M249's handling of excessive accumulated creep.
    return deadline < now ? now + 0.1f : deadline;
}

#endif
