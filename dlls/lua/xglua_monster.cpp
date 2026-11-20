/***
*
*	XGLua — CLuaMonster: Simplified Lua-driven NPC for HL1 (M7-7D)
*
*	Design:
*	- Thin C++ wrapper around CBaseMonster; bypasses brittle Schedule/Task
*	  state machines in favor of Lua-driven Think() + movement/sight helpers.
*	- All Lua callbacks run inside XGLua_ProtectedCall.
*
****/

#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "basemonster.h"
#include "weapons.h"
#include "xglua.h"
#include "xglua_safe.h"
#include "xglua_sound_precache.h"
extern "C" {
#include "lauxlib.h"
}

#define XGLUA_NPC_REGISTRY "XGLua_NPCTables"
#define XGLUA_MAX_NPC_CLASSES 64

struct XGLuaNPCInfo
{
	char classname[64];
	char model[128];
	float health;
	float yawSpeed;
	int classify;
	int bloodColor;
};

static XGLuaNPCInfo g_NPCs[XGLUA_MAX_NPC_CLASSES];
static int g_nNPCs = 0;

static XGLuaNPCInfo *XGLua_FindNPCInfo( const char *classname )
{
	if( !classname || !classname[0] ) return NULL;
	for( int i = 0; i < g_nNPCs; ++i )
	{
		if( !stricmp( g_NPCs[i].classname, classname ) )
			return &g_NPCs[i];
	}
	return NULL;
}

BOOL XGLua_IsLuaNPC( const char *classname )
{
	return XGLua_FindNPCInfo( classname ) != NULL ? TRUE : FALSE;
}

void XGLua_ClearNPCCache( void )
{
	g_nNPCs = 0;
}

static void GetNPCRegistry( lua_State *L )
{
	lua_pushlightuserdata( L, (void *)XGLUA_NPC_REGISTRY );
	lua_rawget( L, LUA_REGISTRYINDEX );
	if( lua_istable( L, -1 ) ) return;
	lua_pop( L, 1 );
	lua_newtable( L );
	lua_pushvalue( L, -1 );
	lua_pushlightuserdata( L, (void *)XGLUA_NPC_REGISTRY );
	lua_insert( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

//===========================================================================
// CLuaMonster
//===========================================================================

class CLuaMonster : public CBaseMonster
{
public:
	char m_szLuaClass[64];
	int m_iClassify;
	float m_flLuaNextThink;
	float m_flCorpseFadeDelay;
	void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType );
	void PlayActivity( int activity );

	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return m_iClassify; }
	int BloodColor( void ) { return m_bloodColor; }
	void MonsterThink( void );
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );

	void InitFromCache( const char *classname );
};

struct XGLua_NPCCallCtx
{
	CLuaMonster *npc;
	const char *method;
	int extraArgs;
	edict_t *ent1;
	edict_t *ent2;
	float f1;
	int i1;
	float outThinkDelay;
};

static int XGLua_NPCCallCore( lua_State *L, void *ud )
{
	XGLua_NPCCallCtx *ctx = (XGLua_NPCCallCtx *)ud;
	GetNPCRegistry( L );
	lua_pushstring( L, ctx->npc->m_szLuaClass );
	lua_rawget( L, -2 );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 2 );
		return 0;
	}
	lua_getfield( L, -1, ctx->method );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 3 );
		return 0;
	}
	lua_remove( L, -2 ); // npc table
	lua_remove( L, -2 ); // registry

	XGLua_PushEntity( L, ctx->npc->edict() );
	int nargs = 1;
	if( ctx->extraArgs >= 2 )
	{
		if( !FNullEnt( ctx->ent1 ) ) XGLua_PushEntity( L, ctx->ent1 ); else lua_pushnil( L );
		if( !FNullEnt( ctx->ent2 ) ) XGLua_PushEntity( L, ctx->ent2 ); else lua_pushnil( L );
		nargs += 2;
	}
	if( ctx->extraArgs >= 4 )
	{
		lua_pushnumber( L, ctx->f1 );
		lua_pushinteger( L, ctx->i1 );
		nargs += 2;
	}

	if( lua_pcall( L, nargs, 1, 0 ) != 0 )
		return lua_error( L );

	if( lua_isnumber( L, -1 ) )
	{
		float d = (float)lua_tonumber( L, -1 );
		if( std::isfinite( d ) && d >= 0.01f && d <= 60.0f )
			ctx->outThinkDelay = d;
	}
	lua_pop( L, 1 );
	return 0;
}

static float CallNPCMethod( CLuaMonster *npc, const char *method, int extraArgs = 0,
	edict_t *ent1 = NULL, edict_t *ent2 = NULL, float f1 = 0.0f, int i1 = 0 )
{
	if( !g_xglua_L || !npc )
		return 0.1f;
	XGLua_NPCCallCtx ctx = { npc, method, extraArgs, ent1, ent2, f1, i1, 0.1f };
	char label[128];
	snprintf( label, sizeof( label ), "NPC:%s", method );
	XGLua_ProtectedCall( g_xglua_L, XGLua_NPCCallCore, &ctx, XGLua_ReportError, label );
	return ctx.outThinkDelay;
}

void CLuaMonster::TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType )
{
	if( pev->takedamage )
	{
		if( m_bloodColor != DONT_BLEED )
		{
			SpawnBlood( ptr->vecEndPos, m_bloodColor, flDamage );
			TraceBleed( flDamage, vecDir, ptr, bitsDamageType );
		}
		AddMultiDamage( pevAttacker, this, flDamage, bitsDamageType );
	}
}

void CLuaMonster::PlayActivity( int activity )
{
	int seq = LookupActivity( activity );
	if( seq < 0 && activity == ACT_RUN )
		seq = LookupActivity( ACT_WALK ); // models without RUN: fall back to WALK
	if( seq < 0 && activity == ACT_DIESIMPLE )
	{
		seq = LookupActivity( ACT_DIEBACKWARD );
		if( seq < 0 )
			seq = LookupActivity( ACT_DIEFORWARD );
	}
	if( seq >= 0 && pev->sequence != seq )
	{
		pev->sequence = seq;
		pev->frame = 0;
		ResetSequenceInfo();
	}
}

void CLuaMonster::InitFromCache( const char *classname )
{
	strncpy( m_szLuaClass, classname, sizeof( m_szLuaClass ) - 1 );
	m_szLuaClass[sizeof( m_szLuaClass ) - 1] = '\0';

	XGLuaNPCInfo *info = XGLua_FindNPCInfo( classname );
	m_iClassify = info ? info->classify : 3;
	m_bloodColor = info ? info->bloodColor : BLOOD_COLOR_RED;
	pev->health = info ? info->health : 100.0f;
	pev->max_health = pev->health;
	pev->yaw_speed = info ? info->yawSpeed : 180.0f;
	m_flLuaNextThink = 0.0f;
	m_flCorpseFadeDelay = 6.0f;
}

void CLuaMonster::Precache( void )
{
	// Models/sounds precached during XGLua_BuildNPCCache at ServerActivate.
}

void CLuaMonster::Spawn( void )
{
	Precache();
	XGLuaNPCInfo *info = XGLua_FindNPCInfo( m_szLuaClass );
	if( info && info->model[0] )
		SET_MODEL( ENT( pev ), info->model );

	UTIL_SetSize( pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX );
	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	pev->takedamage = DAMAGE_AIM;
	pev->deadflag = DEAD_NO;
	pev->flags |= FL_MONSTER;
	pev->gravity = 1.0f;
	pev->ideal_yaw = pev->angles.y;

	ResetSequenceInfo();

	SetThink( &CBaseMonster::CallMonsterThink );
	pev->nextthink = gpGlobals->time + 0.1f;

	CallNPCMethod( this, "Initialize" );
}

void CLuaMonster::MonsterThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;

	StudioFrameAdvance();
	DispatchAnimEvents();

	if( pev->deadflag == DEAD_NO )
	{
		ChangeYaw( (int)pev->yaw_speed );
		if( gpGlobals->time >= m_flLuaNextThink )
		{
			float delay = CallNPCMethod( this, "Think" );
			m_flLuaNextThink = gpGlobals->time + delay;
		}
	}
	else if( pev->deadflag == DEAD_DYING )
	{
		if( m_fSequenceFinished )
		{
			pev->deadflag = DEAD_DEAD;
			StopAnimation();
			if( m_flCorpseFadeDelay > 0.0f )
			{
				SetThink( &CBaseEntity::SUB_StartFadeOut );
				pev->nextthink = gpGlobals->time + m_flCorpseFadeDelay;
			}
		}
	}
}

int CLuaMonster::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	int ret = CBaseMonster::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
	// Do not invoke OnTakeDamage after a lethal hit has already transitioned
	// into Killed() (prevents flinch animations from overwriting death sequence).
	if( pev->deadflag == DEAD_NO )
	{
		edict_t *att = pevAttacker ? ENT( pevAttacker ) : NULL;
		edict_t *inf = pevInflictor ? ENT( pevInflictor ) : NULL;
		CallNPCMethod( this, "OnTakeDamage", 4, att, inf, flDamage, bitsDamageType );
	}
	return ret;
}

void CLuaMonster::Killed( entvars_t *pevAttacker, int iGib )
{
	if( pev->deadflag != DEAD_NO )
		return;

	pev->deadflag = DEAD_DYING;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;
	pev->movetype = MOVETYPE_TOSS;

	// Default to standard death activity; Lua OnKilled can override via SetSequence/SetActivity
	PlayActivity( ACT_DIESIMPLE );

	edict_t *att = pevAttacker ? ENT( pevAttacker ) : NULL;
	CallNPCMethod( this, "OnKilled", 2, att, NULL );
}

CBaseEntity *XGLua_SpawnLuaNPC( const char *classname, const Vector &origin )
{
	if( !XGLua_IsLuaNPC( classname ) )
		return NULL;

	edict_t *pent = CREATE_ENTITY();
	if( FNullEnt( pent ) )
		return NULL;

	entvars_t *pev = VARS( pent );
	pev->classname = MAKE_STRING( classname );
	pev->origin = origin;

	CLuaMonster *npc = new( pev ) CLuaMonster();
	npc->pev = pev;
	npc->InitFromCache( classname );
	return npc;
}

//===========================================================================
// Lua NPC methods (registered on XGLua_Entity metatable)
//===========================================================================

static CLuaMonster *NPCFromLua( lua_State *L, int idx )
{
	edict_t *pent = XGLua_CheckEntity( L, idx );
	CBaseEntity *pEnt = CBaseEntity::Instance( pent );
	if( !pEnt || !XGLua_IsLuaNPC( STRING( pEnt->pev->classname ) ) )
		luaL_error( L, "Tried to use a non-NPC entity as an NPC!" );
	return (CLuaMonster *)pEnt;
}

static int NPC_MoveToPos( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	Vector goal = XGLua_CheckVector( L, 2 );
	float dist = (float)luaL_optnumber( L, 3, 16.0 );
	if( dist > 0.0f )
		UTIL_MoveToOrigin( npc->edict(), goal, dist, MOVE_NORMAL );
	return 0;
}

static int NPC_MoveStep( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	float dist = (float)luaL_checknumber( L, 2 );
	int moved = WALK_MOVE( npc->edict(), npc->pev->angles.y, dist, WALKMOVE_NORMAL );
	lua_pushboolean( L, moved != 0 );
	return 1;
}

static int NPC_SetIdealYaw( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	npc->pev->ideal_yaw = (float)luaL_checknumber( L, 2 );
	return 0;
}

static int NPC_TurnToward( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	Vector target = XGLua_CheckVector( L, 2 );
	npc->MakeIdealYaw( target );
	return 0;
}

static int NPC_DropToFloor( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	lua_pushboolean( L, DROP_TO_FLOOR( npc->edict() ) != 0 );
	return 1;
}

static int NPC_SetSequence( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	int seq = (int)luaL_checkinteger( L, 2 );
	if( npc->pev->sequence != seq )
	{
		npc->pev->sequence = seq;
		npc->pev->frame = 0;
		npc->ResetSequenceInfo();
	}
	return 0;
}

static int NPC_GetSequence( lua_State *L )
{
	lua_pushinteger( L, NPCFromLua( L, 1 )->pev->sequence );
	return 1;
}

static int NPC_IsSequenceFinished( lua_State *L )
{
	lua_pushboolean( L, NPCFromLua( L, 1 )->m_fSequenceFinished != FALSE );
	return 1;
}

static int NPC_CanSeeEntity( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	edict_t *target = XGLua_TryEntity( L, 2 );
	if( !target || target->free )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	TraceResult tr;
	Vector start = npc->pev->origin + npc->pev->view_ofs;
	Vector end = VARS( target )->origin + VARS( target )->view_ofs;
	UTIL_TraceLine( start, end, ignore_monsters, npc->edict(), &tr );
	lua_pushboolean( L, tr.flFraction >= 1.0f || tr.pHit == target );
	return 1;
}

static int NPC_SetEnemy( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	edict_t *target = lua_isnil( L, 2 ) ? NULL : XGLua_TryEntity( L, 2 );
	npc->m_hEnemy = target ? CBaseEntity::Instance( target ) : NULL;
	return 0;
}

static int NPC_GetEnemy( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	CBaseEntity *enemy = npc->m_hEnemy;
	if( !enemy || !enemy->edict() || enemy->edict()->free )
	{
		lua_pushnil( L );
		return 1;
	}
	XGLua_PushEntity( L, enemy->edict() );
	return 1;
}

static int NPC_SetClassify( lua_State *L )
{
	NPCFromLua( L, 1 )->m_iClassify = (int)luaL_checkinteger( L, 2 );
	return 0;
}

static int NPC_SetBloodColor( lua_State *L )
{
	NPCFromLua( L, 1 )->m_bloodColor = (int)luaL_checkinteger( L, 2 );
	return 0;
}

static int NPC_LookupSequence( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	const char *label = luaL_checkstring( L, 2 );
	lua_pushinteger( L, npc->LookupSequence( label ) );
	return 1;
}

static int NPC_LookupActivity( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	int act = (int)luaL_checkinteger( L, 2 );
	lua_pushinteger( L, npc->LookupActivity( act ) );
	return 1;
}

static int NPC_SetActivity( lua_State *L )
{
	CLuaMonster *npc = NPCFromLua( L, 1 );
	int act = (int)luaL_checkinteger( L, 2 );
	npc->PlayActivity( act );
	return 0;
}

void XGLua_RegisterNPCLibs( lua_State *L )
{
	luaL_getmetatable( L, "XGLua_Entity" );
	if( lua_istable( L, -1 ) )
	{
		static const luaL_Reg npcmeta[] =
		{
			{ "MoveToPos",          NPC_MoveToPos },
			{ "MoveStep",           NPC_MoveStep },
			{ "SetIdealYaw",        NPC_SetIdealYaw },
			{ "TurnToward",         NPC_TurnToward },
			{ "DropToFloor",        NPC_DropToFloor },
			{ "SetSequence",        NPC_SetSequence },
			{ "GetSequence",        NPC_GetSequence },
			{ "IsSequenceFinished", NPC_IsSequenceFinished },
			{ "CanSeeEntity",       NPC_CanSeeEntity },
			{ "SetEnemy",           NPC_SetEnemy },
			{ "GetEnemy",           NPC_GetEnemy },
			{ "SetClassify",        NPC_SetClassify },
			{ "SetBloodColor",      NPC_SetBloodColor },
			{ "LookupSequence",     NPC_LookupSequence },
			{ "LookupActivity",     NPC_LookupActivity },
			{ "SetActivity",        NPC_SetActivity },
			{ NULL, NULL }
		};
		for( const luaL_Reg *f = npcmeta; f->name; ++f )
		{
			lua_pushcfunction( L, f->func );
			lua_setfield( L, -2, f->name );
		}
	}
	lua_pop( L, 1 );

	lua_pushinteger( L, ACT_IDLE );          lua_setglobal( L, "ACT_IDLE" );
	lua_pushinteger( L, ACT_WALK );          lua_setglobal( L, "ACT_WALK" );
	lua_pushinteger( L, ACT_RUN );           lua_setglobal( L, "ACT_RUN" );
	lua_pushinteger( L, ACT_RANGE_ATTACK1 ); lua_setglobal( L, "ACT_RANGE_ATTACK1" );
	lua_pushinteger( L, ACT_RANGE_ATTACK2 ); lua_setglobal( L, "ACT_RANGE_ATTACK2" );
	lua_pushinteger( L, ACT_MELEE_ATTACK1 ); lua_setglobal( L, "ACT_MELEE_ATTACK1" );
	lua_pushinteger( L, ACT_MELEE_ATTACK2 ); lua_setglobal( L, "ACT_MELEE_ATTACK2" );
	lua_pushinteger( L, ACT_RELOAD );        lua_setglobal( L, "ACT_RELOAD" );
	lua_pushinteger( L, ACT_ARM );           lua_setglobal( L, "ACT_ARM" );
	lua_pushinteger( L, ACT_DISARM );        lua_setglobal( L, "ACT_DISARM" );
	lua_pushinteger( L, ACT_SMALL_FLINCH );  lua_setglobal( L, "ACT_SMALL_FLINCH" );
	lua_pushinteger( L, ACT_BIG_FLINCH );    lua_setglobal( L, "ACT_BIG_FLINCH" );
	lua_pushinteger( L, ACT_DIESIMPLE );     lua_setglobal( L, "ACT_DIESIMPLE" );
	lua_pushinteger( L, ACT_DIEBACKWARD );   lua_setglobal( L, "ACT_DIEBACKWARD" );
	lua_pushinteger( L, ACT_DIEFORWARD );    lua_setglobal( L, "ACT_DIEFORWARD" );
}

//===========================================================================
// Build NPC C++ cache & precache models/sounds at ServerActivate
//===========================================================================

static int XGLua_BuildNPCCacheCore( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_getglobal( L, "npc" );
	if( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); return 0; }
	lua_getfield( L, -1, "GetStored" );
	if( !lua_isfunction( L, -1 ) ) { lua_pop( L, 2 ); return 0; }
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
	{
		lua_pop( L, 1 );
		return 0;
	}

	lua_pushnil( L );
	while( lua_next( L, -2 ) != 0 )
	{
		if( lua_isstring( L, -2 ) && lua_istable( L, -1 ) && g_nNPCs < XGLUA_MAX_NPC_CLASSES )
		{
			const char *classname = lua_tostring( L, -2 );
			lua_getfield( L, -1, "Abstract" );
			bool isAbstract = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );

			if( !isAbstract )
			{
				XGLuaNPCInfo info;
				memset( &info, 0, sizeof( info ) );
				strncpy( info.classname, classname, sizeof( info.classname ) - 1 );

				lua_getfield( L, -1, "Model" );
				if( lua_isstring( L, -1 ) )
					strncpy( info.model, lua_tostring( L, -1 ), sizeof( info.model ) - 1 );
				lua_pop( L, 1 );

				lua_getfield( L, -1, "Health" );
				info.health = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : 100.0f;
				lua_pop( L, 1 );

				lua_getfield( L, -1, "YawSpeed" );
				info.yawSpeed = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : 180.0f;
				lua_pop( L, 1 );

				lua_getfield( L, -1, "Classify" );
				info.classify = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : 3;
				lua_pop( L, 1 );

				lua_getfield( L, -1, "BloodColor" );
				info.bloodColor = lua_isnumber( L, -1 ) ? (int)lua_tonumber( L, -1 ) : BLOOD_COLOR_RED;
				lua_pop( L, 1 );

				// Precache optional sounds table
				lua_getfield( L, -1, "PrecacheSounds" );
				if( lua_istable( L, -1 ) )
				{
					int n = (int)lua_objlen( L, -1 );
					for( int i = 1; i <= n; ++i )
					{
						lua_rawgeti( L, -1, i );
						if( lua_isstring( L, -1 ) )
							PRECACHE_SOUND( (char *)lua_tostring( L, -1 ) );
						lua_pop( L, 1 );
					}
				}
				lua_pop( L, 1 );

				if( info.model[0] )
					PRECACHE_MODEL( info.model );

				GetNPCRegistry( L );
				lua_pushstring( L, info.classname );
				lua_pushvalue( L, -3 );
				lua_rawset( L, -3 );
				lua_pop( L, 1 );

				g_NPCs[g_nNPCs++] = info;
				ALERT( at_console, "[XGLua] registered NPC: %s\n", info.classname );
			}
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 2 );
	return 0;
}

void XGLua_BuildNPCCache( lua_State *L )
{
	g_nNPCs = 0;
	XGLua_ProtectedCall( L, XGLua_BuildNPCCacheCore, NULL, XGLua_ReportError, "NPC cache build" );
}
