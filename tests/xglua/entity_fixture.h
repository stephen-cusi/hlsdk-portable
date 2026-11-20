// Shared game-boundary fixture for the entity-side host tests.
//
// The REAL production code under test is compiled into each test TU:
//   xglua_types.cpp (Vector/Angle userdata), xglua_entity_handles.h,
//   xglua_entity_spatial.h, xglua_entity_geometry.h, xglua_entity_state.h,
//   xglua_player_api.h, xglua_player_extra.h, xglua_entity_methods.h,
//   xglua_entity_queries.h, xglua_util_trace.h.
// Only the game/engine boundary is stubbed (edicts, entvars, CBasePlayer,
// engine callbacks). The metatable installed by SetupEntityFixture mirrors
// XGLua_RegisterEntityLibs in dlls/lua/xglua_entity.cpp for every method
// whose implementation lives in a header; methods implemented only in
// xglua_entity.cpp (GetPos/Health/Give/...) are reviewed statically and are
// NOT bound here.
//
// World layout (16 slots, maxClients 4):
//   0 worldspawn, 1 player "Alice", 2 bot player "Bot", 3 item_healthkit,
//   4 weapon_lua_test (Lua SWEP), 5 weapon_9mmhandgun (native),
//   6..7 free spares, 8..15 weapon-item slots used by addweapon().

#ifndef XGLUA_ENTITY_FIXTURE_H
#define XGLUA_ENTITY_FIXTURE_H

#include "test_framework.h"
#include <cstddef>
#include <climits>
#include <cfloat>

//---------------------------------------------------------------------------
// Vector + the REAL production types code (xglua.h is game-side; suppress it
// and provide the only thing xglua_types.cpp needs from it: Vector).
//---------------------------------------------------------------------------
#define XGLUA_H
struct Vector
{
	float x, y, z;
	Vector( float a = 0, float b = 0, float c = 0 ) : x( a ), y( b ), z( c ) {}
	Vector operator+( const Vector &r ) const { return Vector( x + r.x, y + r.y, z + r.z ); }
	Vector operator*( float s ) const { return Vector( x * s, y * s, z * s ); }
};
#include "xglua_types.cpp"

//---------------------------------------------------------------------------
// game structs, constants, string_t
//---------------------------------------------------------------------------
struct edict_t;
struct entvars_t
{
	Vector origin, mins, maxs, size, absmin, absmax, velocity, angles;
	Vector v_angle, punchangle, view_ofs;
	float health, max_health, frags, armorvalue;
	int flags, movetype, solid, waterlevel, button, deadflag, spawnflags;
	edict_t *groundentity, *owner;
	const char *classname, *targetname, *netname, *model;
};
struct edict_t { bool free; int serialnumber; void *pvPrivateData; entvars_t v; };

typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DEAD_NO 0
#define MAX_ITEM_TYPES 6
#define FL_GODMODE (1<<6)
#define FL_ONGROUND (1<<9)
#define FL_FROZEN (1<<12)
#define FL_FAKECLIENT (1<<13)
#define FL_DUCKING (1<<14)
#define FL_KILLME (1<<30)
#define PFLAG_ONTRAIN (1<<1)
#define PFLAG_USING (1<<4)

typedef const char *string_t;
#define STRING(s) (s)
#define MAKE_STRING(x) (x)
#define iStringNull ((const char *)NULL)
#define FStringNull(x) ((x)==iStringNull)
#define VARS(e) (&(e)->v)

struct Globals { int maxEntities, maxClients; const char *mapname; Vector v_forward, v_right, v_up; };
static Globals globals;
static Globals *gpGlobals = &globals;

struct CBaseEntity
{
	bool isPlayer;
	static CBaseEntity *Instance( edict_t *e ) { return (CBaseEntity *)e->pvPrivateData; }
	bool IsPlayer() const { return isPlayer; }
	void SUB_Remove();
};
struct CBasePlayerItem;
struct CBasePlayer
{
	edict_t *ent; entvars_t *pev;
	int m_afButtonPressed, m_afButtonReleased, m_afPhysicsFlags, m_iFOV, m_iDeaths;
	void *m_pTank;
	CBasePlayerItem *m_rgpPlayerItems[MAX_ITEM_TYPES];
	bool observer, hasWeapons; const char *team;
	edict_t *edict() { return ent; }
	int IsObserver() const { return observer ? 1 : 0; }
	const char *TeamID() { return team; }
	int HasWeapons() { return hasWeapons ? 1 : 0; }
	Vector GetGunPosition() { return pev->origin + pev->view_ofs; }
};
struct CBasePlayerItem { edict_t *ent; entvars_t *pev; CBasePlayerItem *m_pNext; edict_t *edict() { return ent; } };

#define XGLUA_ENTITY_META "XGLua_Entity"
struct XGLuaEntRef { edict_t *pent; int serial; };
static edict_t *XGLua_ValidateEnt( XGLuaEntRef *r )
{
	return r && r->pent && !r->pent->free && r->pent->serialnumber == r->serial ? r->pent : NULL;
}
#include "xglua_entity_handles.h"

//---------------------------------------------------------------------------
// engine boundary
//---------------------------------------------------------------------------
#define FX_SLOTS 16
static edict_t slots[FX_SLOTS];
static CBaseEntity objects[FX_SLOTS];
static CBasePlayer players[FX_SLOTS];
static CBasePlayerItem items[8];

static int indexEntCalls;
static int ENTINDEX( edict_t *e ) { return (int)( e - slots ); }
static edict_t *INDEXENT( int i ) { ++indexEntCalls; return i >= 0 && i < FX_SLOTS ? &slots[i] : NULL; }

// Exact mirror of XGLua_CheckEntity in dlls/lua/xglua_entity.cpp.
static edict_t *XGLua_CheckEntity( lua_State *L, int idx )
{
	XGLuaEntRef *r = (XGLuaEntRef *)luaL_checkudata( L, idx, XGLUA_ENTITY_META );
	edict_t *e = XGLua_ValidateEnt( r );
	if( !e ) luaL_error( L, "Tried to use a NULL entity!" );
	return e;
}

static int spawnCalls, removeCalls, originCalls, sizeCalls;
static edict_t *lastSpawn, *lastRemove, *lastOrigin, *lastSize;
void CBaseEntity::SUB_Remove()
{
	++removeCalls;
	for( int i = 0; i < FX_SLOTS; ++i )
		if( slots[i].pvPrivateData == this ) { lastRemove = &slots[i]; break; }
}
static void DispatchSpawn( edict_t *e ) { ++spawnCalls; lastSpawn = e; }
static void UTIL_SetOrigin( entvars_t *v, const Vector &pos )
{
	++originCalls; lastOrigin = (edict_t *)( (char *)v - offsetof( edict_t, v ) ); v->origin = pos;
}
static void UTIL_SetSize( entvars_t *v, const Vector &mins, const Vector &maxs )
{
	++sizeCalls; lastSize = (edict_t *)( (char *)v - offsetof( edict_t, v ) );
	v->mins = mins; v->maxs = maxs;
	v->size = Vector( maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z );
}
#include "xglua_entity_spatial.h"	// defines the real XGLua_PushEntity

static CBasePlayer *Ply( lua_State *L, int idx )
{
	edict_t *e = XGLua_CheckEntity( L, idx );
	CBaseEntity *o = CBaseEntity::Instance( e );
	if( !o || !o->IsPlayer() ) luaL_error( L, "Tried to use a non-player entity as a Player!" );
	return &players[e - slots];
}
static BOOL FClassnameIs( entvars_t *pev, const char *name )
{
	return pev->classname && !std::strcmp( pev->classname, name ) ? TRUE : FALSE;
}

struct TraceResult;
struct EngFuncs
{
	void (*pfnGetPlayerStats)( edict_t *, int *, int * );
	int (*pfnGetPlayerUserId)( edict_t * );
	void (*pfnTraceLine)( const Vector &, const Vector &, int, edict_t *, TraceResult * );
	void (*pfnTraceHull)( const Vector &, const Vector &, int, int, edict_t *, TraceResult * );
};
static EngFuncs g_engfuncs;
#define PLAYER_CNX_STATS ( *g_engfuncs.pfnGetPlayerStats )
#define GETPLAYERUSERID ( *g_engfuncs.pfnGetPlayerUserId )
#define TRACE_LINE ( *g_engfuncs.pfnTraceLine )
#define TRACE_HULL ( *g_engfuncs.pfnTraceHull )

static int userIds[FX_SLOTS];
static int fakeUserId( edict_t *e ) { return userIds[ENTINDEX( e )]; }
static edict_t *lastStatsEnt;
static int statsPing, statsLoss;
static void fakeStats( edict_t *e, int *ping, int *loss ) { lastStatsEnt = e; *ping = statsPing; *loss = statsLoss; }

// GetAimVector must use v_angle through the private helper and must NOT
// touch gpGlobals' shared direction vectors. The stub returns a fixed
// marker direction and records the angles it was given.
static Vector lastAimAngles;
static int aimCalls;
static void UTIL_MakeVectorsPrivate( const Vector &angles, Vector &forward, Vector *, Vector * )
{
	++aimCalls; lastAimAngles = angles; forward = Vector( 0.5f, 0.25f, 0.125f );
}

#include "xglua_entity_geometry.h"
#include "xglua_entity_state.h"
#include "xglua_player_api.h"
#include "xglua_player_extra.h"

//---------------------------------------------------------------------------
// SWEP registry boundary (mirrors xglua_vm.cpp: registry table keyed by
// classname; IsLuaWeapon = class present in the registry)
//---------------------------------------------------------------------------
static char luaWeaponClasses[8][64];
static int luaWeaponCount;
static const char *XGLua_EntityClassName( edict_t *pent ) { return STRING( VARS( pent )->classname ); }
static bool XGLua_IsLuaWeapon( const char *cls )
{
	if( !cls ) return false;
	for( int i = 0; i < luaWeaponCount; ++i )
		if( !std::strcmp( luaWeaponClasses[i], cls ) ) return true;
	return false;
}
static void XGLua_GetSWEPRegistry( lua_State *L ) { lua_getfield( L, LUA_REGISTRYINDEX, "FixtureSWEPRegistry" ); }
// This fixture does not register scripted entity classes.
static bool XGLua_IsLuaEntity( const char * ) { return false; }
static void XGLua_GetSENTRegistry( lua_State *L ) { lua_newtable( L ); }
// Ent_Index's stale path hands out IsValid; mirror of the cpp-side binding.
static int Ent_IsValid( lua_State *L )
{
	XGLuaEntRef *r = XGLua_TestEntityRef( L, 1 );
	lua_pushboolean( L, r && XGLua_ValidateEnt( r ) != NULL );
	return 1;
}
#include "xglua_entity_methods.h"

#include "xglua_entity_queries.h"

//---------------------------------------------------------------------------
// trace engine boundary
//---------------------------------------------------------------------------
struct TraceResult
{
	int fAllSolid, fStartSolid, fInOpen, fInWater;
	float flFraction; Vector vecEndPos; float flPlaneDist; Vector vecPlaneNormal;
	edict_t *pHit; int iHitgroup;
};
static TraceResult canned;
static int traceCalls, lastFlags, lastHull;
static edict_t *lastSkip;
static Vector lastTraceStart, lastTraceEnd;
static void fakeTraceLine( const Vector &s, const Vector &e, int flags, edict_t *skip, TraceResult *tr )
{
	++traceCalls; lastFlags = flags; lastSkip = skip; lastHull = -1;
	lastTraceStart = s; lastTraceEnd = e;
	*tr = canned; tr->vecEndPos = e;
}
static void fakeTraceHull( const Vector &s, const Vector &e, int flags, int hull, edict_t *skip, TraceResult *tr )
{
	++traceCalls; lastFlags = flags; lastSkip = skip; lastHull = hull;
	lastTraceStart = s; lastTraceEnd = e;
	*tr = canned; tr->vecEndPos = e;
}
#include "xglua_util_trace.h"

//---------------------------------------------------------------------------
// fixture control helpers exposed to Lua (compact stubs, named descriptively)
//---------------------------------------------------------------------------
static int fixtureCursor;
static int nextItem;

static int fx_indexcalls( lua_State *L ) { lua_pushinteger( L, indexEntCalls ); return 1; }
static int fx_slot( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); if( i < 0 || i >= FX_SLOTS ) return luaL_argerror( L, 1, "slot out of range" ); XGLua_PushEntity( L, &slots[i] ); return 1; }
static int fx_invalidate( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].free = true; return 0; }
static int fx_reuse( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); ++slots[i].serialnumber; slots[i].free = false; return 0; }
static int fx_setflags( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.flags |= (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_clearflags( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.flags &= ~(int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setpriv( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); slots[i].pvPrivateData = lua_toboolean( L, 2 ) ? &objects[i] : NULL; return 0; }
static int fx_setabs( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.absmin = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	slots[i].v.absmax = Vector( (float)luaL_checknumber( L, 5 ), (float)luaL_checknumber( L, 6 ), (float)luaL_checknumber( L, 7 ) );
	return 0;
}
static int fx_setorigin( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.origin = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setmins( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.mins = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setmaxs( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.maxs = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setground( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); slots[i].v.groundentity = lua_isnil( L, 2 ) ? NULL : &slots[(int)luaL_checkinteger( L, 2 )]; return 0; }
static int fx_settargetname( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); slots[i].v.targetname = lua_isnil( L, 2 ) ? NULL : luaL_checkstring( L, 2 ); return 0; }
static int fx_setclassname( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); slots[i].v.classname = luaL_checkstring( L, 2 ); return 0; }
static int fx_setnetname( lua_State *L ) { int i = (int)luaL_checkinteger( L, 1 ); slots[i].v.netname = luaL_checkstring( L, 2 ); return 0; }
static int fx_setfrags( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.frags = (float)luaL_checknumber( L, 2 ); return 0; }
static int fx_setarmor( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.armorvalue = (float)luaL_checknumber( L, 2 ); return 0; }
static int fx_setbutton( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.button = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setmovetype( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.movetype = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setsolid( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.solid = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setwater( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.waterlevel = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setmaxhealth( lua_State *L ) { slots[(int)luaL_checkinteger( L, 1 )].v.max_health = (float)luaL_checknumber( L, 2 ); return 0; }
static int fx_setvangle( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.v_angle = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setpunch( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.punchangle = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setviewofs( lua_State *L )
{
	int i = (int)luaL_checkinteger( L, 1 );
	slots[i].v.view_ofs = Vector( (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ), (float)luaL_checknumber( L, 4 ) );
	return 0;
}
static int fx_setpressed( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_afButtonPressed = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setreleased( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_afButtonReleased = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setphysics( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_afPhysicsFlags = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setfov( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_iFOV = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_setdeaths( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_iDeaths = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_settank( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].m_pTank = lua_toboolean( L, 2 ) ? (void *)1 : NULL; return 0; }
static int fx_setobserver( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].observer = lua_toboolean( L, 2 ); return 0; }
static int fx_setteam( lua_State *L ) { players[(int)luaL_checkinteger( L, 1 )].team = lua_isnil( L, 2 ) ? NULL : luaL_checkstring( L, 2 ); return 0; }
static int fx_maxclients( lua_State *L ) { globals.maxClients = (int)luaL_checkinteger( L, 1 ); return 0; }
static int fx_setmap( lua_State *L ) { globals.mapname = lua_isnil( L, 1 ) ? NULL : luaL_checkstring( L, 1 ); return 0; }
static int fx_setuid( lua_State *L ) { userIds[(int)luaL_checkinteger( L, 1 )] = (int)luaL_checkinteger( L, 2 ); return 0; }
static int fx_useridcb( lua_State *L ) { g_engfuncs.pfnGetPlayerUserId = lua_toboolean( L, 1 ) ? fakeUserId : NULL; return 0; }
static int fx_statscb( lua_State *L ) { g_engfuncs.pfnGetPlayerStats = lua_toboolean( L, 1 ) ? fakeStats : NULL; return 0; }
static int fx_statsent( lua_State *L ) { lua_pushinteger( L, lastStatsEnt ? ENTINDEX( lastStatsEnt ) : -1 ); return 1; }
static int fx_addweapon( lua_State *L )
{
	int ply = (int)luaL_checkinteger( L, 1 ), slotNo = (int)luaL_checkinteger( L, 2 );
	const char *cls = luaL_checkstring( L, 3 );
	if( nextItem >= 8 ) return luaL_error( L, "fixture item pool exhausted" );
	int n = nextItem++;
	int si = 8 + n;
	items[n].ent = &slots[si]; items[n].pev = &slots[si].v; items[n].m_pNext = NULL;
	slots[si].free = false; slots[si].pvPrivateData = &objects[si]; slots[si].v.classname = cls;
	CBasePlayerItem **head = &players[ply].m_rgpPlayerItems[slotNo];
	while( *head ) head = &( *head )->m_pNext;
	*head = &items[n];
	players[ply].hasWeapons = true;
	lua_pushinteger( L, si );
	return 1;
}
static int fx_clearweapons( lua_State *L )
{
	int ply = (int)luaL_checkinteger( L, 1 );
	for( int s = 0; s < MAX_ITEM_TYPES; ++s ) players[ply].m_rgpPlayerItems[s] = NULL;
	players[ply].hasWeapons = false;
	return 0;
}
static int fx_traceCfg( lua_State *L )
{
	canned.flFraction = (float)luaL_checknumber( L, 1 );
	canned.iHitgroup = (int)luaL_checknumber( L, 2 );
	canned.fStartSolid = (int)luaL_checknumber( L, 3 );
	canned.fAllSolid = (int)luaL_checknumber( L, 4 );
	canned.fInOpen = (int)luaL_checknumber( L, 5 );
	canned.fInWater = (int)luaL_checknumber( L, 6 );
	int hit = (int)luaL_checknumber( L, 7 );
	canned.pHit = hit < 0 ? NULL : &slots[hit];
	canned.vecPlaneNormal = Vector( (float)luaL_checknumber( L, 8 ), (float)luaL_checknumber( L, 9 ), (float)luaL_checknumber( L, 10 ) );
	return 0;
}
static int fx_traceInfo( lua_State *L )
{
	lua_pushinteger( L, traceCalls ); lua_pushinteger( L, lastFlags );
	lua_pushinteger( L, lastHull ); lua_pushinteger( L, lastSkip ? ENTINDEX( lastSkip ) : -1 );
	return 4;
}
static int fx_traceReset( lua_State *L ) { traceCalls = 0; lastFlags = -1; lastHull = -99; lastSkip = NULL; return 0; }
static int fx_engineTrace( lua_State *L )
{
	g_engfuncs.pfnTraceLine = lua_toboolean( L, 1 ) ? fakeTraceLine : NULL;
	g_engfuncs.pfnTraceHull = lua_toboolean( L, 1 ) ? fakeTraceHull : NULL;
	return 0;
}
static int fx_sweep( lua_State *L )
{
	int budget = (int)luaL_optinteger( L, 1, 8 );
	XGLua_SweepEntityHandles( L, globals.maxEntities, budget, &fixtureCursor );
	return 0;
}
static int fx_swepSet( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TTABLE );
	if( luaWeaponCount < 8 ) snprintf( luaWeaponClasses[luaWeaponCount++], 64, "%s", cls );
	XGLua_GetSWEPRegistry( L );
	lua_pushvalue( L, 2 );
	lua_setfield( L, -2, cls );
	lua_pop( L, 1 );
	return 0;
}
static int fx_aimInfo( lua_State *L )
{
	lua_pushinteger( L, aimCalls );
	lua_pushnumber( L, lastAimAngles.x ); lua_pushnumber( L, lastAimAngles.y ); lua_pushnumber( L, lastAimAngles.z );
	return 4;
}
static int fx_engineCounts( lua_State *L )
{
	lua_pushinteger( L, spawnCalls ); lua_pushinteger( L, removeCalls );
	lua_pushinteger( L, originCalls ); lua_pushinteger( L, sizeCalls );
	return 4;
}
static int fx_lastEngineEnts( lua_State *L )
{
	lua_pushinteger( L, lastOrigin ? ENTINDEX( lastOrigin ) : -1 );
	lua_pushinteger( L, lastSize ? ENTINDEX( lastSize ) : -1 );
	lua_pushinteger( L, lastSpawn ? ENTINDEX( lastSpawn ) : -1 );
	return 3;
}

// Mirrors the header-implemented subset of XGLua_RegisterEntityLibs.
static int Ent_EntIndex( lua_State *L ) { lua_pushinteger( L, ENTINDEX( XGLua_CheckEntity( L, 1 ) ) ); return 1; }
static int Ent_GetClass( lua_State *L ) { lua_pushstring( L, STRING( VARS( XGLua_CheckEntity( L, 1 ) )->classname ) ); return 1; }
static int Ent_IsPlayer( lua_State *L )
{
	CBaseEntity *o = CBaseEntity::Instance( XGLua_CheckEntity( L, 1 ) );
	lua_pushboolean( L, o && o->IsPlayer() );
	return 1;
}
static int player_GetAll( lua_State *L )
{
	lua_newtable( L );
	int count = 0;
	for( int i = 1; i <= globals.maxClients; ++i )
	{
		edict_t *ent = XGLua_QueryPlayer( i );
		if( ent ) XGLua_AddQueryResult( L, ent, count );
	}
	return 1;
}

static void ResetWorld()
{
	std::memset( slots, 0, sizeof( slots ) );
	std::memset( objects, 0, sizeof( objects ) );
	std::memset( players, 0, sizeof( players ) );
	std::memset( items, 0, sizeof( items ) );
	std::memset( userIds, 0, sizeof( userIds ) );
	std::memset( &canned, 0, sizeof( canned ) );
	std::memset( &g_engfuncs, 0, sizeof( g_engfuncs ) );
	globals.maxEntities = FX_SLOTS; globals.maxClients = 4; globals.mapname = NULL;
	globals.v_forward = Vector( 9, 9, 9 ); globals.v_right = Vector( 8, 8, 8 ); globals.v_up = Vector( 7, 7, 7 );
	spawnCalls = removeCalls = originCalls = sizeCalls = 0;
	lastSpawn = lastRemove = lastOrigin = lastSize = lastStatsEnt = lastSkip = NULL;
	traceCalls = 0; lastFlags = -1; lastHull = -99;
	aimCalls = 0; lastAimAngles = Vector();
	fixtureCursor = 0; nextItem = 0; luaWeaponCount = 0; indexEntCalls = 0;
	statsPing = 42; statsLoss = 7;
	for( int i = 0; i < FX_SLOTS; ++i )
	{
		slots[i].free = true;
		players[i].ent = &slots[i]; players[i].pev = &slots[i].v;
	}
	static const char *classes[6] = { "worldspawn", "player", "player", "item_healthkit", "weapon_lua_test", "weapon_9mmhandgun" };
	for( int i = 0; i < 6; ++i )
	{
		slots[i].free = false;
		slots[i].pvPrivateData = &objects[i];
		slots[i].v.classname = classes[i];
	}
	objects[1].isPlayer = objects[2].isPlayer = true;
	// Keep the non-player props far from the origin so spatial-query cases
	// that only place bounds on slots 1/2 stay deterministic.
	for( int i = 3; i <= 5; ++i )
	{
		slots[i].v.absmin = Vector( 5000 + i * 10, 5000, 5000 );
		slots[i].v.absmax = Vector( 5010 + i * 10, 5010, 5010 );
	}
	slots[1].v.netname = "Alice"; slots[2].v.netname = "Bot";
	slots[2].v.flags = FL_FAKECLIENT;
	slots[1].v.health = 100; slots[2].v.health = 100;
	slots[1].v.mins = Vector( -16, -16, -36 ); slots[1].v.maxs = Vector( 16, 16, 36 );
	userIds[1] = 5; userIds[2] = 6;
	g_engfuncs.pfnGetPlayerUserId = fakeUserId;
	g_engfuncs.pfnGetPlayerStats = fakeStats;
	g_engfuncs.pfnTraceLine = fakeTraceLine;
	g_engfuncs.pfnTraceHull = fakeTraceHull;
}

static bool SetupEntityFixture( lua_State *L )
{
	ResetWorld();
	XGLua_RegisterTypes( L );

	luaL_newmetatable( L, XGLUA_ENTITY_META );
	static const luaL_Reg entmeta[] = {
		{ "IsValid", Ent_IsValid }, { "EntIndex", Ent_EntIndex }, { "GetClass", Ent_GetClass },
		{ "IsPlayer", Ent_IsPlayer },
		{ "GetFeetPos", Ent_GetFeetPos }, { "WorldSpaceCenter", Ent_WorldSpaceCenter },
		{ "OBBMins", Ent_OBBMins }, { "OBBMaxs", Ent_OBBMaxs }, { "GetCollisionBounds", Ent_GetCollisionBounds },
		{ "SetPos", Ent_SetPos }, { "SetCollisionBounds", Ent_SetCollisionBounds },
		{ "GetMaxHealth", Ent_GetMaxHealth },
		{ "GetGroundEntity", Ent_GetGroundEntity },
		{ "GetMoveType", Ent_GetMoveType }, { "GetSolid", Ent_GetSolid }, { "WaterLevel", Ent_WaterLevel },
		{ "OnGround", Ent_OnGround }, { "IsOnGround", Ent_OnGround },
		{ "Crouching", Ply_Crouching }, { "Armor", Ply_Armor }, { "Ping", Ply_Ping },
		{ "KeyDown", Ply_KeyDown }, { "KeyPressed", Ply_KeyPressed }, { "KeyReleased", Ply_KeyReleased },
		{ "Spawn", Ent_Spawn }, { "Remove", Ent_Remove },
		{ "Name", Ply_Name },
		{ "IsObserver", Ply_IsObserver }, { "GetFOV", Ply_GetFOV }, { "IsOnTrain", Ply_IsOnTrain },
		{ "IsUsing", Ply_IsUsing }, { "InVehicle", Ply_InVehicle }, { "IsFrozen", Ply_IsFrozen },
		{ "IsGodMode", Ply_IsGodMode },
		{ "Frags", Ply_Frags }, { "Deaths", Ply_Deaths }, { "Team", Ply_Team },
		{ "IsBot", Ply_IsBot }, { "IsConnected", Ply_IsConnected },
		{ "HasWeapons", Ply_HasWeapons }, { "GetWeapons", Ply_GetWeapons }, { "HasWeapon", Ply_HasWeapon },
		{ "UserID", Ply_UserID },
		{ "GetAimVector", Ply_GetAimVector }, { "ShootPos", Ply_ShootPos }, { "EyeAngles", Ply_EyeAngles },
		{ "__index", Ent_Index }, { "__newindex", Ent_NewIndex },
		{ NULL, NULL } };
	for( const luaL_Reg *f = entmeta; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setfield( L, -2, f->name ); }
	lua_pop( L, 1 );

	static const luaL_Reg entslib[] = {
		{ "GetAll", ents_GetAll }, { "FindByClass", ents_FindByClass },
		{ "FindByName", ents_FindByName }, { "FindInSphere", ents_FindInSphere }, { "FindInBox", ents_FindInBox },
		{ NULL, NULL } };
	lua_newtable( L );
	for( const luaL_Reg *f = entslib; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setfield( L, -2, f->name ); }
	lua_setglobal( L, "ents" );

	static const luaL_Reg playerlib[] = {
		{ "GetAll", player_GetAll }, { "GetBots", player_GetBots }, { "GetHumans", player_GetHumans },
		{ "GetByUserID", player_GetByUserID }, { NULL, NULL } };
	lua_newtable( L );
	for( const luaL_Reg *f = playerlib; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setfield( L, -2, f->name ); }
	lua_setglobal( L, "player" );

	static const luaL_Reg gamelib[] = {
		{ "GetMap", game_GetMap }, { "MaxPlayers", game_MaxPlayers }, { "GetWorld", game_GetWorld }, { NULL, NULL } };
	lua_newtable( L );
	for( const luaL_Reg *f = gamelib; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setfield( L, -2, f->name ); }
	lua_setglobal( L, "game" );

	static const luaL_Reg utillib[] = {
		{ "TraceLine", util_TraceLine }, { "TraceHull", util_TraceHull }, { NULL, NULL } };
	lua_newtable( L );
	for( const luaL_Reg *f = utillib; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setfield( L, -2, f->name ); }
	lua_setglobal( L, "util" );

	lua_pushcfunction( L, Entity_Global ); lua_setglobal( L, "Entity" );
	lua_pushcfunction( L, Ent_IsValid ); lua_setglobal( L, "IsValid" );

	lua_newtable( L ); lua_setfield( L, LUA_REGISTRYINDEX, "FixtureSWEPRegistry" );

	static const luaL_Reg helpers[] = {
		{ "slot", fx_slot }, { "indexcalls", fx_indexcalls }, { "invalidate", fx_invalidate }, { "reuse", fx_reuse },
		{ "setflags", fx_setflags }, { "clearflags", fx_clearflags }, { "setpriv", fx_setpriv },
		{ "setabs", fx_setabs }, { "setorigin", fx_setorigin }, { "setmins", fx_setmins }, { "setmaxs", fx_setmaxs },
		{ "setground", fx_setground }, { "settargetname", fx_settargetname }, { "setclassname", fx_setclassname },
		{ "setnetname", fx_setnetname }, { "setfrags", fx_setfrags }, { "setarmor", fx_setarmor },
		{ "setbutton", fx_setbutton }, { "setmovetype", fx_setmovetype }, { "setsolid", fx_setsolid },
		{ "setwater", fx_setwater }, { "setmaxhealth", fx_setmaxhealth },
		{ "setvangle", fx_setvangle }, { "setpunch", fx_setpunch }, { "setviewofs", fx_setviewofs },
		{ "setpressed", fx_setpressed }, { "setreleased", fx_setreleased }, { "setphysics", fx_setphysics },
		{ "setfov", fx_setfov }, { "setdeaths", fx_setdeaths }, { "settank", fx_settank },
		{ "setobserver", fx_setobserver }, { "setteam", fx_setteam },
		{ "maxclients", fx_maxclients }, { "setmap", fx_setmap },
		{ "setuid", fx_setuid }, { "useridcb", fx_useridcb }, { "statscb", fx_statscb }, { "statsent", fx_statsent },
		{ "addweapon", fx_addweapon }, { "clearweapons", fx_clearweapons },
		{ "traceCfg", fx_traceCfg }, { "traceInfo", fx_traceInfo }, { "traceReset", fx_traceReset },
		{ "engineTrace", fx_engineTrace }, { "sweep", fx_sweep }, { "swepSet", fx_swepSet },
		{ "aimInfo", fx_aimInfo }, { "engineCounts", fx_engineCounts }, { "lastEngineEnts", fx_lastEngineEnts },
		{ NULL, NULL } };
	for( const luaL_Reg *f = helpers; f->name; ++f ) { lua_pushcfunction( L, f->func ); lua_setglobal( L, f->name ); }
	return true;
}

#endif // XGLUA_ENTITY_FIXTURE_H