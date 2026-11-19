#ifndef XGLUA_GAMERULES_H
#define XGLUA_GAMERULES_H

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"

// CHalfLifeMultiplay subclass forwarding CGameRules virtuals to Lua hooks
// (hook.Add -> GAMEMODE fallback -> CHalfLifeMultiplay vanilla fallback).
//
// Virtuals ALREADY dispatched at dedicated engine/player hooks are intentionally
// NOT overridden here to avoid double-dispatch:
//   - Think                  (XGLua_StartFrame)
//   - PlayerSpawn / Loadout  (CBasePlayer::Spawn + CHalfLifeMultiplay::PlayerSpawn)
//   - PlayerDeath            (CBasePlayer::Killed)
//   - PlayerDisconnected     (ClientDisconnect)
//   - PlayerShouldTakeDamage (CBasePlayer::TakeDamage)
//   - PlayerCanPickupWeapon  (CBasePlayerItem::AddToPlayer)
class CLuaGameRules : public CHalfLifeMultiplay
{
public:
	CLuaGameRules();
	virtual ~CLuaGameRules() {}

	virtual BOOL FAllowMonsters( void ) override;
	virtual edict_t *GetPlayerSpawnSpot( CBasePlayer *pPlayer ) override;
	virtual BOOL FPlayerCanRespawn( CBasePlayer *pPlayer ) override;
	virtual float FlPlayerSpawnTime( CBasePlayer *pPlayer ) override;
	virtual float FlPlayerFallDamage( CBasePlayer *pPlayer ) override;
	virtual int IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled ) override;
	virtual BOOL FShouldSwitchWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon ) override;
	virtual BOOL ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target ) override;
};

#endif // XGLUA_GAMERULES_H
