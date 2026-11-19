/***
*
*	XGLua — CLuaEntity: GMod-style scripted entities (scripted_ents) for HL1
*
*	Design (per doc/XGLua_Design.md §9.1-9.3, M2):
*	- Generic C++ class CLuaEntity : CBaseAnimating
*	- Script creation via ents.Create() uses CREATE_ENTITY() + placement new
*	  onto the entvars block (GetClassPtr pattern — private data is allocated
*	  by the engine right after the entvars block), bypassing HL1's
*	  export-symbol lookup; identical to XGLua_SpawnLuaWeapon
*	- Map placement via exported classname "lua_entity" + keyvalue "luaclass"
*	- Lifecycle virtuals (Spawn/KeyValue/Think/Touch/Use/TakeDamage/
*	  UpdateOnRemove) dispatch to ENT:* inside XGLua_ProtectedCall
*	- The engine think loop is nextthink-driven: Spawn arms a 0.1s tick only
*	  for classes that define ENT:Think; a script may re-arm it any time with
*	  Entity:SetNextThink — the fixed cadence is only applied when the script
*	  left nextthink untouched during the callback
*	- Save/Restore is not supported in v1 (design doc §9.1)
*
****/

#include <cstring>
#include <cstdio>
#include <new>

#include "xglua.h"
#include "xglua_sent_bridge.h"
#include "xglua_sound_precache.h"
#include "player.h"
extern "C" {
#include "lauxlib.h"
}

//===========================================================================
// SENT C++ cache — built once at startup from scripted_ents.GetStored()
//===========================================================================

#define XGLUA_SENT_MAX_SOUNDS 8
#define XGLUA_SENT_SOUND_BYTES XGLUA_SOUND_PATH_BYTES

struct XGLuaSENTInfo
{
	char classname[64];
	char type[16];     // "anim" | "point" | "brush"
	char model[128];   // optional ENT.Model precached at startup
	bool hasThink;     // ENT defines a Think function -> Spawn arms the tick
	int soundCount;
	char sounds[XGLUA_SENT_MAX_SOUNDS][XGLUA_SENT_SOUND_BYTES]; // ENT.PrecacheSounds
};

#define XGLUA_MAX_SENT 128
static XGLuaSENTInfo g_SENTs[XGLUA_MAX_SENT];
static int g_nSENTs = 0;

// Same discipline as the SWEP cache: declared sound paths are validated
// (path shape + file existence) before anything is precached, and a shared
// dedup registry avoids double-precaching names already registered.
static char g_SENTSounds[XGLUA_SOUND_TOTAL][XGLUA_SOUND_PATH_BYTES];
static int g_nSENTSounds = 0;

static bool XGLua_SENTSoundExists( const char *name )
{
	char path[XGLUA_SOUND_PATH_BYTES + 6];
	snprintf( path, sizeof( path ), "sound/%s", name );
	if( g_engfuncs.pfnGetFileSize ) return g_engfuncs.pfnGetFileSize( path ) > 0;
	if( !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile ) return false;
	int length = 0;
	byte *data = LOAD_FILE_FOR_ME( path, &length );
	if( !data ) return false;
	FREE_FILE( data );
	return length > 0;
}

static XGLuaSENTInfo *XGLua_FindSENT( const char *classname )
{
	if( !classname || !classname[0] )
		return NULL;
	for( int i = 0; i < g_nSENTs; ++i )
	{
		if( !strcmp( g_SENTs[i].classname, classname ) )
			return &g_SENTs[i];
	}
	return NULL;
}

void XGLua_GetSENTRegistry( lua_State *L )
{
	XGLua_GetSENTRegistryImpl( L );
}

BOOL XGLua_IsLuaEntity( const char *classname )
{
	if( !g_xglua_L || !classname || !classname[0] )
		return FALSE;
	return XGLua_IsLuaEntityImpl( g_xglua_L, classname, XGLua_ReportError ) ? TRUE : FALSE;
}

void XGLua_ClearEntityCache( void )
{
	g_nSENTs = 0;
}

//===========================================================================
// CLuaEntity
//===========================================================================

class CLuaEntity : public CBaseAnimating
{
public:
	char m_szLuaClass[64];

	void InitLuaClass( const char *classname );

	void Spawn( void ) override;
	void KeyValue( KeyValueData *pkvd ) override;
	void Think( void ) override;
	void Touch( CBaseEntity *pOther ) override;
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) override;
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType ) override;
	void UpdateOnRemove( void ) override;
};

// Map-placed generic entry point: classname "lua_entity" + keyvalue "luaclass"
LINK_ENTITY_TO_CLASS( lua_entity, CLuaEntity );

void CLuaEntity::InitLuaClass( const char *classname )
{
	if( !classname )
	{
		m_szLuaClass[0] = '\0';
		return;
	}
	snprintf( m_szLuaClass, sizeof( m_szLuaClass ), "%s", classname );
}

//---------------------------------------------------------------------------
// Argument pushers (executed inside XGLua_ProtectedCall)
//---------------------------------------------------------------------------

static int XGLua_PushSelfEnt( lua_State *L, void *ctx )
{
	XGLua_PushEntity( L, ( (CLuaEntity *)ctx )->edict() );
	return 1;
}

struct XGLua_KeyValueArgs { CLuaEntity *self; const char *key; const char *value; };
static int XGLua_PushKeyValueArgs( lua_State *L, void *ctx )
{
	XGLua_KeyValueArgs *a = (XGLua_KeyValueArgs *)ctx;
	XGLua_PushEntity( L, a->self->edict() );
	lua_pushstring( L, a->key ? a->key : "" );
	lua_pushstring( L, a->value ? a->value : "" );
	return 3;
}

struct XGLua_TouchArgs { CLuaEntity *self; CBaseEntity *other; };
static int XGLua_PushTouchArgs( lua_State *L, void *ctx )
{
	XGLua_TouchArgs *a = (XGLua_TouchArgs *)ctx;
	XGLua_PushEntity( L, a->self->edict() );
	if( a->other && !FNullEnt( a->other->edict() ) )
		XGLua_PushEntity( L, a->other->edict() );
	else
		lua_pushnil( L );
	return 2;
}

struct XGLua_UseArgs { CLuaEntity *self; CBaseEntity *activator; CBaseEntity *caller; int useType; float value; };
static int XGLua_PushUseArgs( lua_State *L, void *ctx )
{
	XGLua_UseArgs *a = (XGLua_UseArgs *)ctx;
	XGLua_PushEntity( L, a->self->edict() );
	if( a->activator && !FNullEnt( a->activator->edict() ) )
		XGLua_PushEntity( L, a->activator->edict() );
	else
		lua_pushnil( L );
	if( a->caller && !FNullEnt( a->caller->edict() ) )
		XGLua_PushEntity( L, a->caller->edict() );
	else
		lua_pushnil( L );
	lua_pushinteger( L, a->useType );
	lua_pushnumber( L, a->value );
	return 5;
}

// Read-only DamageInfo view table for ENT:OnTakeDamage(dmginfo).
// Stores values directly in the Lua table so it never holds C++ pointers.
static int XGLua_SENTDmg_GetDamage( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	lua_getfield( L, 1, "_damage" );
	return 1;
}
static int XGLua_SENTDmg_GetDamageType( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	lua_getfield( L, 1, "_type" );
	return 1;
}
static int XGLua_SENTDmg_GetAttacker( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	lua_getfield( L, 1, "_attacker" );
	return 1;
}
static int XGLua_SENTDmg_GetInflictor( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TTABLE );
	lua_getfield( L, 1, "_inflictor" );
	return 1;
}

struct XGLua_SENTDmgArgs { CLuaEntity *self; entvars_t *inflictor; entvars_t *attacker; float damage; int bitsType; };
static int XGLua_PushSENTDmgArgs( lua_State *L, void *ctx )
{
	XGLua_SENTDmgArgs *a = (XGLua_SENTDmgArgs *)ctx;
	XGLua_PushEntity( L, a->self->edict() );

	lua_newtable( L );
	lua_pushnumber( L, a->damage );
	lua_setfield( L, -2, "_damage" );
	lua_pushinteger( L, a->bitsType );
	lua_setfield( L, -2, "_type" );
	if( a->attacker && !FNullEnt( a->attacker ) )
		XGLua_PushEntity( L, ENT( a->attacker ) );
	else
		lua_pushnil( L );
	lua_setfield( L, -2, "_attacker" );
	if( a->inflictor && !FNullEnt( a->inflictor ) )
		XGLua_PushEntity( L, ENT( a->inflictor ) );
	else
		lua_pushnil( L );
	lua_setfield( L, -2, "_inflictor" );

	lua_pushcfunction( L, XGLua_SENTDmg_GetDamage );
	lua_setfield( L, -2, "GetDamage" );
	lua_pushcfunction( L, XGLua_SENTDmg_GetDamageType );
	lua_setfield( L, -2, "GetDamageType" );
	lua_pushcfunction( L, XGLua_SENTDmg_GetAttacker );
	lua_setfield( L, -2, "GetAttacker" );
	lua_pushcfunction( L, XGLua_SENTDmg_GetInflictor );
	lua_setfield( L, -2, "GetInflictor" );
	return 2;
}

//---------------------------------------------------------------------------
// CLuaEntity virtuals
//---------------------------------------------------------------------------

void CLuaEntity::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "luaclass" ) )
	{
		InitLuaClass( pkvd->szValue );
		pkvd->fHandled = TRUE;
		return;
	}

	if( g_xglua_L && m_szLuaClass[0] )
	{
		XGLua_KeyValueArgs args = { this, pkvd->szKeyName, pkvd->szValue };
		int res = XGLua_CallSENTBoolMethod( g_xglua_L, m_szLuaClass, "KeyValue",
			XGLua_PushKeyValueArgs, &args, XGLua_ReportError, "ENT:KeyValue" );
		if( res == 1 )
		{
			pkvd->fHandled = TRUE;
			return;
		}
	}

	CBaseAnimating::KeyValue( pkvd );
}

void CLuaEntity::Spawn( void )
{
	if( !m_szLuaClass[0] )
		return;

	XGLuaSENTInfo *info = XGLua_FindSENT( m_szLuaClass );
	if( !info )
	{
		ALERT( at_console, "[XGLua] CLuaEntity::Spawn: unknown Lua entity class '%s'\n", m_szLuaClass );
		return;
	}

	pev->classname = MAKE_STRING( info->classname );
	if( info->model[0] )
		SET_MODEL( ENT( pev ), info->model );

	XGLua_CallSENTMethod( g_xglua_L, m_szLuaClass, "Initialize",
		XGLua_PushSelfEnt, this, XGLua_ReportError, "ENT:Initialize" );

	// The engine think loop is nextthink-driven: arm the tick only for
	// classes that define ENT:Think (HL never calls Think on its own).
	if( info->hasThink && pev->nextthink == 0.0f )
		pev->nextthink = gpGlobals->time + 0.1f;
}

void CLuaEntity::Think( void )
{
	if( g_xglua_L && m_szLuaClass[0] )
	{
		const float previous = pev->nextthink;
		XGLua_CallSENTMethod( g_xglua_L, m_szLuaClass, "Think",
			XGLua_PushSelfEnt, this, XGLua_ReportError, "ENT:Think" );
		// Fixed 0.1s cadence unless the script armed a custom nextthink
		// during the callback (Entity:SetNextThink). A self-removed entity
		// (FL_KILLME from inside ENT:Think) must not re-enter the think loop.
		if( !( pev->flags & FL_KILLME ) && pev->nextthink == previous )
			pev->nextthink = gpGlobals->time + 0.1f;
	}
	CBaseAnimating::Think();
}

void CLuaEntity::Touch( CBaseEntity *pOther )
{
	if( g_xglua_L && m_szLuaClass[0] )
	{
		XGLua_TouchArgs args = { this, pOther };
		XGLua_CallSENTMethod( g_xglua_L, m_szLuaClass, "Touch",
			XGLua_PushTouchArgs, &args, XGLua_ReportError, "ENT:Touch" );
	}
	CBaseAnimating::Touch( pOther );
}

void CLuaEntity::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( g_xglua_L && m_szLuaClass[0] )
	{
		XGLua_UseArgs args = { this, pActivator, pCaller, (int)useType, value };
		XGLua_CallSENTMethod( g_xglua_L, m_szLuaClass, "Use",
			XGLua_PushUseArgs, &args, XGLua_ReportError, "ENT:Use" );
	}
	CBaseAnimating::Use( pActivator, pCaller, useType, value );
}

int CLuaEntity::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	if( !pev->takedamage )
		return 0;

	if( g_xglua_L && m_szLuaClass[0] )
	{
		XGLua_SENTDmgArgs args = { this, pevInflictor, pevAttacker, flDamage, bitsDamageType };
		int cancel = XGLua_CallSENTBoolMethod( g_xglua_L, m_szLuaClass, "OnTakeDamage",
			XGLua_PushSENTDmgArgs, &args, XGLua_ReportError, "ENT:OnTakeDamage" );
		if( cancel == 1 )
			return 0;
	}

	// Base CBaseEntity::TakeDamage runs the global EntityTakeDamage hook once
	// (strict true cancels, DamageInfo modifications apply).
	return CBaseAnimating::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

void CLuaEntity::UpdateOnRemove( void )
{
	if( g_xglua_L && m_szLuaClass[0] )
	{
		XGLua_CallSENTMethod( g_xglua_L, m_szLuaClass, "OnRemove",
			XGLua_PushSelfEnt, this, XGLua_ReportError, "ENT:OnRemove" );
	}
	CBaseAnimating::UpdateOnRemove();
}

//===========================================================================
// Dynamic creation (ents.Create)
//===========================================================================

CBaseEntity *XGLua_SpawnLuaEntity( const char *classname, const Vector &origin )
{
	XGLuaSENTInfo *info = XGLua_FindSENT( classname );
	if( !info )
		return NULL;

	edict_t *pent = CREATE_ENTITY();
	if( FNullEnt( pent ) )
		return NULL;

	entvars_t *pev = VARS( pent );
	pev->classname = MAKE_STRING( info->classname );
	pev->origin = origin;

	// placement new — mirrors GetClassPtr<T>() (cbase.h); private data is
	// allocated by the engine right after the entvars block
	CLuaEntity *ent = new( pev ) CLuaEntity();
	ent->pev = pev;
	ent->InitLuaClass( info->classname );
	return ent;
}

//===========================================================================
// Startup cache build: scripted_ents.GetStored() -> g_SENTs + registry
//===========================================================================

struct XGLua_SENTFieldCtx
{
	int tableRef;
	const char *classname;
	XGLuaSENTInfo *slot;
	bool abstract;
	bool validType;
	bool validSounds;
};

static char g_szSENTFieldError[256];
static void XGLua_SENTFieldError( lua_State *L, const char *context )
{
	(void)context;
	snprintf( g_szSENTFieldError, sizeof( g_szSENTFieldError ), "%s", XGLua_ErrorText( L, -1 ) );
	lua_pop( L, 1 );
}

static int XGLua_SENTFieldCore( lua_State *L, void *ud )
{
	XGLua_SENTFieldCtx *fc = (XGLua_SENTFieldCtx *)ud;
	XGLuaSENTInfo *slot = fc->slot;

	lua_rawgeti( L, LUA_REGISTRYINDEX, fc->tableRef );
	const int table = lua_gettop( L );

	fc->abstract = XGLua_SENTIsAbstract( L, table );
	fc->validType = XGLua_ReadSENTType( L, table, slot->type, sizeof( slot->type ) );

	lua_getfield( L, table, "Model" );
	if( lua_type( L, -1 ) == LUA_TSTRING )
		snprintf( slot->model, sizeof( slot->model ), "%s", lua_tostring( L, -1 ) );
	lua_pop( L, 1 );

	lua_getfield( L, table, "Think" );
	slot->hasThink = lua_isfunction( L, -1 ) != 0;
	lua_pop( L, 1 );

	lua_getfield( L, table, "PrecacheSounds" );
	if( lua_istable( L, -1 ) )
	{
		const int n = (int)lua_objlen( L, -1 );
		for( int i = 1; i <= n && slot->soundCount < XGLUA_SENT_MAX_SOUNDS; ++i )
		{
			lua_rawgeti( L, -1, i );
			if( lua_type( L, -1 ) == LUA_TSTRING )
			{
				const char *path = lua_tostring( L, -1 );
				if( !XGLua_ValidSoundPath( path, strlen( path ) ) )
				{
					lua_pop( L, 2 );
					return false;
				}
				snprintf( slot->sounds[slot->soundCount], XGLUA_SENT_SOUND_BYTES, "%s", path );
				++slot->soundCount;
			}
			lua_pop( L, 1 );
		}
	}
	if( slot->soundCount > 0 )
		fc->validSounds = XGLua_ValidateSoundResources( slot->sounds, slot->soundCount,
			g_SENTSounds, g_nSENTSounds, XGLua_SENTSoundExists );
	else
		fc->validSounds = true;
	lua_pop( L, 1 );
	return 0;
}

struct XGLua_StoreSENTCtx { int tableRef; const char *classname; };
static int XGLua_StoreSENTCore( lua_State *L, void *ud )
{
	XGLua_StoreSENTCtx *sc = (XGLua_StoreSENTCtx *)ud;
	lua_rawgeti( L, LUA_REGISTRYINDEX, sc->tableRef );
	if( !XGLua_StoreSENTTable( L, lua_gettop( L ), sc->classname ) )
	{
		lua_pushliteral( L, "failed to store SENT table" );
		return lua_error( L );
	}
	return 0;
}

static int XGLua_BuildEntityCacheCore( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_getglobal( L, "scripted_ents" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "GetStored" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
		return lua_error( L );
	if( !lua_istable( L, -1 ) )
		return 0;

	const int storedIdx = lua_gettop( L );
	lua_pushnil( L );
	while( lua_next( L, storedIdx ) != 0 )
	{
		// [-2] key (classname), [-1] ENT table
		if( lua_type( L, -2 ) == LUA_TSTRING && lua_istable( L, -1 ) )
		{
			const char *classname = lua_tostring( L, -2 );
			if( g_nSENTs >= XGLUA_MAX_SENT )
			{
				ALERT( at_console, "[XGLua] too many SENTs, ignoring %s\n", classname );
				lua_pop( L, 1 );
				continue;
			}

			XGLuaSENTInfo local;
			memset( &local, 0, sizeof( local ) );
			snprintf( local.classname, sizeof( local.classname ), "%s", classname );

			lua_pushvalue( L, -1 );
			const int tableRef = luaL_ref( L, LUA_REGISTRYINDEX );
			XGLua_SENTFieldCtx fc = { tableRef, local.classname, &local, false, false, false };
			g_szSENTFieldError[0] = '\0';

			if( !XGLua_ProtectedCall( L, XGLua_SENTFieldCore, &fc, XGLua_SENTFieldError, classname ) )
			{
				ALERT( at_console, "[XGLua] %s: SENT field read failed, skipped: %s\n",
					classname, g_szSENTFieldError[0] ? g_szSENTFieldError : "unknown error" );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 1 );
				continue;
			}

			if( fc.abstract )
			{
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 1 );
				continue;
			}

			if( !fc.validType )
			{
				ALERT( at_console, "[XGLua] %s: invalid ENT.Type (expected anim/point/brush), skipped\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 1 );
				continue;
			}

			if( !fc.validSounds )
			{
				ALERT( at_console, "[XGLua] %s: invalid, missing, or excessive PrecacheSounds; entity skipped\n", classname );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 1 );
				continue;
			}

			XGLua_StoreSENTCtx sc = { tableRef, local.classname };
			g_szSENTFieldError[0] = '\0';
			if( !XGLua_ProtectedCall( L, XGLua_StoreSENTCore, &sc, XGLua_SENTFieldError, classname ) )
			{
				ALERT( at_console, "[XGLua] %s: SENT registry store failed, skipped: %s\n",
					classname, g_szSENTFieldError[0] ? g_szSENTFieldError : "unknown error" );
				luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
				lua_pop( L, 1 );
				continue;
			}

			// Commit to C++ cache and precache declared resources while in
			// the server spawn phase (same window as the SWEP cache build).
			g_SENTs[g_nSENTs++] = local;
			if( local.model[0] )
				PRECACHE_MODEL( (char *)local.model );
			for( int i = 0; i < local.soundCount; ++i )
			{
				bool known = false;
				for( int j = 0; j < g_nSENTSounds && !known; ++j )
					known = XGLua_SoundNamesEqual( local.sounds[i], g_SENTSounds[j] );
				if( !known )
				{
					PRECACHE_SOUND( (char *)local.sounds[i] );
					strcpy( g_SENTSounds[g_nSENTSounds++], local.sounds[i] );
				}
			}

			ALERT( at_console, "[XGLua] registered SENT: %s (type=%s)\n", local.classname, local.type );
			luaL_unref( L, LUA_REGISTRYINDEX, tableRef );
		}
		lua_pop( L, 1 ); // pop value, keep key for lua_next
	}
	return 0;
}

void XGLua_BuildEntityCache( lua_State *L )
{
	g_nSENTs = 0;
	XGLua_ProtectedCall( L, XGLua_BuildEntityCacheCore, NULL, XGLua_ReportError, "entity cache build" );
}
