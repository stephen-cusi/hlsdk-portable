#ifndef XGLUA_ENTITY_GEOMETRY_H
#define XGLUA_ENTITY_GEOMETRY_H

// Read-only spatial queries over HL1's native entity state. The returned
// Vector values are copies; querying them never moves or relinks an entity.
// HL1 mins/maxs are local axis-aligned collision bounds, not arbitrary
// rotated model OBBs. WorldSpaceCenter uses the engine's world bounds so
// brush models receive the same center as CBaseEntity::Center().

void XGLua_PushAngle( lua_State *L, const Vector &v );

static int Ent_GetFeetPos( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const entvars_t *pev = VARS( pent );
	XGLua_PushVector( L, Vector( pev->origin.x, pev->origin.y, pev->origin.z + pev->mins.z ) );
	return 1;
}

static int Ent_WorldSpaceCenter( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	const entvars_t *pev = VARS( pent );
	XGLua_PushVector( L, ( pev->absmin + pev->absmax ) * 0.5f );
	return 1;
}

static int Ent_OBBMins( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	XGLua_PushVector( L, VARS( pent )->mins );
	return 1;
}

static int Ent_OBBMaxs( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	XGLua_PushVector( L, VARS( pent )->maxs );
	return 1;
}

static int Ent_GetCollisionBounds( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	XGLua_PushVector( L, VARS( pent )->mins );
	XGLua_PushVector( L, VARS( pent )->maxs );
	return 2;
}

static int Ply_ShootPos( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	XGLua_PushVector( L, p->GetGunPosition() );
	return 1;
}

static int Ply_GetAimVector( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	Vector forward;
	// Preserve the existing unassisted v_angle direction (no punchangle or
	// autoaim), but never overwrite gpGlobals' shared direction vectors.
	UTIL_MakeVectorsPrivate( p->pev->v_angle, forward, NULL, NULL );
	XGLua_PushVector( L, forward );
	return 1;
}

static int Ply_EyeAngles( lua_State *L )
{
	CBasePlayer *p = Ply( L, 1 );
	// EyeAngles is an Angle value, unlike the legacy Vector-valued GetAngles
	// binding; return a copy of the native view angles, without punchangle.
	XGLua_PushAngle( L, p->pev->v_angle );
	return 1;
}

#endif // XGLUA_ENTITY_GEOMETRY_H
