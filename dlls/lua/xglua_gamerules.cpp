/***
*
*	XGLua — CLuaGameRules: CHalfLifeMultiplay bridge (design doc §8.1.1)
*
****/

#include "xglua.h"
#include "xglua_gamerules.h"
#include "xglua_gamerules_bridge.h"

static void *XGLua_ExtractEdict( lua_State *L, int idx )
{
	return (void *)XGLua_TryEntity( L, idx );
}

static int XGLua_GRPushOneEnt( lua_State *L, void *ctx )
{
	edict_t *e = (edict_t *)ctx;
	if( e && !FNullEnt( e ) )
		XGLua_PushEntity( L, e );
	else
		lua_pushnil( L );
	return 1;
}

struct XGLua_GRTwoEnts { edict_t *a; edict_t *b; };
static int XGLua_GRPushTwoEnts( lua_State *L, void *ctx )
{
	XGLua_GRTwoEnts *p = (XGLua_GRTwoEnts *)ctx;
	if( p->a && !FNullEnt( p->a ) ) XGLua_PushEntity( L, p->a ); else lua_pushnil( L );
	if( p->b && !FNullEnt( p->b ) ) XGLua_PushEntity( L, p->b ); else lua_pushnil( L );
	return 2;
}

struct XGLua_GRThreeEnts { edict_t *a; edict_t *b; edict_t *c; };
static int XGLua_GRPushThreeEnts( lua_State *L, void *ctx )
{
	XGLua_GRThreeEnts *p = (XGLua_GRThreeEnts *)ctx;
	if( p->a && !FNullEnt( p->a ) ) XGLua_PushEntity( L, p->a ); else lua_pushnil( L );
	if( p->b && !FNullEnt( p->b ) ) XGLua_PushEntity( L, p->b ); else lua_pushnil( L );
	if( p->c && !FNullEnt( p->c ) ) XGLua_PushEntity( L, p->c ); else lua_pushnil( L );
	return 3;
}

CLuaGameRules::CLuaGameRules()
	: CHalfLifeMultiplay()
{
}

BOOL CLuaGameRules::FAllowMonsters( void )
{
	if( g_xglua_L )
	{
		int res = XGLua_HookRunTriBool( g_xglua_L, "AllowMonsters", NULL, NULL, XGLua_ReportError );
		if( res == 1 ) return TRUE;
		if( res == 2 ) return FALSE;
	}
	return CHalfLifeMultiplay::FAllowMonsters();
}

edict_t *CLuaGameRules::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	if( g_xglua_L && pPlayer && pPlayer->edict() )
	{
		edict_t *spot = (edict_t *)XGLua_HookRunEntity( g_xglua_L, "PlayerSelectSpawn",
			XGLua_GRPushOneEnt, pPlayer->edict(), XGLua_ExtractEdict, XGLua_ReportError );
		if( spot && !FNullEnt( spot ) )
		{
			pPlayer->pev->origin = spot->v.origin + Vector( 0, 0, 1 );
			pPlayer->pev->v_angle = g_vecZero;
			pPlayer->pev->velocity = g_vecZero;
			pPlayer->pev->angles = spot->v.angles;
			pPlayer->pev->punchangle = g_vecZero;
			pPlayer->pev->fixangle = TRUE;
			return spot;
		}
	}
	return CHalfLifeMultiplay::GetPlayerSpawnSpot( pPlayer );
}

BOOL CLuaGameRules::FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	if( g_xglua_L && pPlayer && pPlayer->edict() )
	{
		int res = XGLua_HookRunTriBool( g_xglua_L, "PlayerCanRespawn",
			XGLua_GRPushOneEnt, pPlayer->edict(), XGLua_ReportError );
		if( res == 1 ) return TRUE;
		if( res == 2 ) return FALSE;
	}
	return CHalfLifeMultiplay::FPlayerCanRespawn( pPlayer );
}

float CLuaGameRules::FlPlayerSpawnTime( CBasePlayer *pPlayer )
{
	if( g_xglua_L && pPlayer && pPlayer->edict() )
	{
		float t = 0.0f;
		if( XGLua_HookRunFloat( g_xglua_L, "PlayerSpawnTime",
			XGLua_GRPushOneEnt, pPlayer->edict(), &t, XGLua_ReportError ) )
			return t;
	}
	return CHalfLifeMultiplay::FlPlayerSpawnTime( pPlayer );
}

float CLuaGameRules::FlPlayerFallDamage( CBasePlayer *pPlayer )
{
	if( g_xglua_L && pPlayer && pPlayer->edict() )
	{
		float dmg = 0.0f;
		if( XGLua_HookRunFloat( g_xglua_L, "GetFallDamage",
			XGLua_GRPushOneEnt, pPlayer->edict(), &dmg, XGLua_ReportError ) )
			return dmg;
	}
	return CHalfLifeMultiplay::FlPlayerFallDamage( pPlayer );
}

int CLuaGameRules::IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled )
{
	if( g_xglua_L )
	{
		XGLua_GRTwoEnts args = {
			pAttacker ? pAttacker->edict() : NULL,
			pKilled ? pKilled->edict() : NULL
		};
		int pts = 0;
		if( XGLua_HookRunInt( g_xglua_L, "GetPointsForKill",
			XGLua_GRPushTwoEnts, &args, &pts, XGLua_ReportError ) )
			return pts;
	}
	return CHalfLifeMultiplay::IPointsForKill( pAttacker, pKilled );
}

BOOL CLuaGameRules::FShouldSwitchWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if( g_xglua_L && pPlayer && pWeapon )
	{
		XGLua_GRThreeEnts args = {
			pPlayer->edict(),
			pPlayer->m_pActiveItem ? pPlayer->m_pActiveItem->edict() : NULL,
			pWeapon->edict()
		};
		int res = XGLua_HookRunTriBool( g_xglua_L, "PlayerSwitchWeapon",
			XGLua_GRPushThreeEnts, &args, XGLua_ReportError );
		if( res == 1 ) return TRUE;
		if( res == 2 ) return FALSE;
	}
	return CHalfLifeMultiplay::FShouldSwitchWeapon( pPlayer, pWeapon );
}

BOOL CLuaGameRules::ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target )
{
	if( g_xglua_L && pPlayer )
	{
		XGLua_GRTwoEnts args = { pPlayer->edict(), target };
		int res = XGLua_HookRunTriBool( g_xglua_L, "ShouldAutoAim",
			XGLua_GRPushTwoEnts, &args, XGLua_ReportError );
		if( res == 1 ) return TRUE;
		if( res == 2 ) return FALSE;
	}
	return CHalfLifeMultiplay::ShouldAutoAim( pPlayer, target );
}
