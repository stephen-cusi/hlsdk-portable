/***
*
*	XGLua — PlayerUse / PlayerShouldTakeDamage / EntityTakeDamage glue
*
*	Thin game-side wrappers over the edict-only implementations in
*	xglua_damage.h (the only game TU that includes it). Invalid or empty
*	inflictor/attacker entities map to NULL here, so the Lua layer never
*	sees a dangling edict.
*
*	TakeDamage integration map (see the batch report for the full call
*	graph): the hook fires at the entry of every SELF-CONTAINED TakeDamage
*	override - CBaseEntity, CBaseMonster (players skipped: their own entry
*	covers them, and CBasePlayer::TakeDamage explicitly calls the monster
*	base), CBasePlayer, CBaseButton, CButtonTarget, CBaseTurret, CBreakable,
*	CCycler, CCyclerSprite. Overrides that forward to a base (apache ->
*	CBaseEntity; bloater/barnacle/bullsquid/bigmomma/controller/hgrunt/
*	hornet -> CBaseMonster; plus CBaseMonster::DeadTakeDamage called only
*	from the monster entry) are covered exactly once through the base they
*	forward to. Entities with takedamage == DAMAGE_NO never dispatch.
*
****/

#include "xglua.h"
#include "player.h"	// CBasePlayer::edict
#include "lua/xglua_damage.h"

bool XGLua_DamageEntityValid( edict_t *pent, int serial )
{
	return pent && !pent->free && pent->serialnumber == serial && !(pent->v.flags & FL_KILLME);
}

// Continuation-safe check for inflictor/attacker: FL_KILLME entities keep
// their entvars until the end of the frame, so a script removing the
// projectile that hit does NOT cancel the damage it caused. Only a freed or
// serial-reused edict (entvars possibly owned by a different entity now)
// makes continuing into the native body unsafe.
static bool XGLua_DamageEntityAlive( edict_t *pent, int serial )
{
	return pent && !pent->free && pent->serialnumber == serial;
}

int XGLua_EntityTakeDamage( CBaseEntity *victim, entvars_t *pevInflictor, entvars_t *pevAttacker,
	float *pflDamage, int *pbitsType )
{
	if( !g_xglua_L || !victim || !pflDamage || !pbitsType )
		return 0;

	edict_t *inflictor = ( pevInflictor && !FNullEnt( pevInflictor ) ) ? ENT( pevInflictor ) : NULL;
	edict_t *attacker = ( pevAttacker && !FNullEnt( pevAttacker ) ) ? ENT( pevAttacker ) : NULL;

	if( !victim->pev )
		return 0;
	edict_t *target = victim->edict();
	const int targetSerial = target->serialnumber;
	const int inflictorSerial = inflictor ? inflictor->serialnumber : 0;
	const int attackerSerial = attacker ? attacker->serialnumber : 0;
	XGLuaDmgCtx ctx = { target, inflictor, attacker, pflDamage, pbitsType, 0, inflictorSerial, attackerSerial };
	int cancelled = XGLua_EntityTakeDamageImpl( g_xglua_L, &ctx );
	if( !XGLua_DamageEntityValid( target, targetSerial ) ||
		(inflictor && !XGLua_DamageEntityAlive( inflictor, inflictorSerial )) ||
		(attacker && !XGLua_DamageEntityAlive( attacker, attackerSerial )) )
		return 1;
	return cancelled;
}

BOOL XGLua_PlayerShouldTakeDamage( CBasePlayer *pPlayer, entvars_t *pevInflictor, entvars_t *pevAttacker )
{
	if( !g_xglua_L || !pPlayer )
		return TRUE;
	edict_t *attacker = ( pevAttacker && !FNullEnt( pevAttacker ) ) ? ENT( pevAttacker ) : NULL;
	edict_t *inflictor = ( pevInflictor && !FNullEnt( pevInflictor ) ) ? ENT( pevInflictor ) : NULL;
	if( !pPlayer->pev )
		return TRUE;
	edict_t *player = pPlayer->edict();
	const int playerSerial = player->serialnumber;
	const int attackerSerial = attacker ? attacker->serialnumber : 0;
	const int inflictorSerial = inflictor ? inflictor->serialnumber : 0;
	bool denied = XGLua_PlayerShouldTakeDamageImpl( g_xglua_L, player, attacker );
	return denied || !XGLua_DamageEntityValid( player, playerSerial ) ||
		(attacker && !XGLua_DamageEntityAlive( attacker, attackerSerial )) ||
		(inflictor && !XGLua_DamageEntityAlive( inflictor, inflictorSerial )) ? FALSE : TRUE;
}

BOOL XGLua_PlayerUse( CBasePlayer *pPlayer, CBaseEntity *pTarget )
{
	if( !g_xglua_L || !pPlayer || !pTarget )
		return TRUE;
	if( !pPlayer->pev || !pTarget->pev )
		return TRUE;
	edict_t *player = pPlayer->edict();
	edict_t *target = pTarget->edict();
	const int playerSerial = player->serialnumber;
	const int targetSerial = target->serialnumber;
	bool denied = XGLua_PlayerUseImpl( g_xglua_L, player, target );
	return denied || !XGLua_DamageEntityValid( player, playerSerial ) ||
		!XGLua_DamageEntityValid( target, targetSerial ) ? FALSE : TRUE;
}
