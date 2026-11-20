#pragma once

#include <cmath>

inline bool XGLua_ValidBulletDamage( double damage )
{
	return std::isfinite( damage ) && damage >= 1 && damage <= 1000 && std::floor( damage ) == damage;
}
inline bool XGLua_ValidBulletCount( double count )
{
	return std::isfinite( count ) && count >= 1 && count <= 32 && std::floor( count ) == count;
}
inline bool XGLua_ValidBulletSpread( const Vector &spread )
{
	return std::isfinite( spread.x ) && std::isfinite( spread.y ) && std::isfinite( spread.z ) &&
		spread.x >= 0 && spread.x <= 1 && spread.y >= 0 && spread.y <= 1;
}

// Kept engine-independent apart from the player/vector interface so the same
// dispatch path can be exercised with an instrumented player in native tests.
template <class Player>
inline void XGLua_FirePlayerBullets( Player *player, int damage, int count, const Vector &spread, int damageFlags = -1 )
{
	UTIL_MakeVectors( player->pev->v_angle + player->pev->punchangle );
	const Vector direction = gpGlobals->v_forward;
	player->FireBulletsPlayer( (ULONG)count, player->GetGunPosition(), direction,
		spread, 8192.0f, BULLET_PLAYER_9MM, 0, damage, player->pev, player->random_seed, damageFlags );
}
