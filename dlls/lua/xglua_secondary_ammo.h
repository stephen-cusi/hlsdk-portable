#ifndef XGLUA_SECONDARY_AMMO_H
#define XGLUA_SECONDARY_AMMO_H

// The GoldSrc ammo registry uses indices 1..MAX_AMMO_SLOTS-1; zero is reserved.
static inline bool XGLua_SecondaryPickupAllowed( bool alive, bool luaWeapon, bool owned,
    int defaultAmmo, int index, int slotCount, int current, int capacity )
{
    return alive && luaWeapon && owned && defaultAmmo > 0 && index > 0 &&
        index < slotCount && current < capacity;
}

// Native AddDuplicate uses ExtractAmmo only with nonzero primary default ammo.
// In the clip-only path it returns GiveAmmo's index or -1. Secondary must be
// added separately on this path, but never twice on the ExtractAmmo path.
template<class Primary, class Secondary>
static inline int XGLua_DuplicateAmmo( bool primaryDefault, bool primaryType,
    Primary primary, Secondary secondary )
{
    if( primaryDefault ) return primary() != 0;
    int result = primaryType && primary() > 0;
    return result | ( secondary() != 0 );
}

static inline int XGLua_SecondaryRemaining( int current, int amount )
{
    if( amount <= 0 ) return current;
    return amount >= current ? 0 : current - amount;
}

static inline float XGLua_WeaponIdleDeadline( float now, float delay )
{
    return now + delay;
}
#endif
