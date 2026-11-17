#ifndef XGLUA_ENTITY_SPATIAL_H
#define XGLUA_ENTITY_SPATIAL_H

// World is a real Lua entity; GoldSrc FNullEnt intentionally treats its
// offset as a null sentinel and must still be used at those other call sites.
void XGLua_PushEntity( lua_State *L, edict_t *pent )
{
    if( !pent || pent->free ) { lua_pushnil( L ); return; }
    static int sweepIndex = 0;
    int maxEntities = gpGlobals->maxEntities;
    if( maxEntities <= 0 ) maxEntities = 1;
    if( sweepIndex >= maxEntities ) sweepIndex = 0;
    XGLua_PushEntityHandle( L, pent, ENTINDEX( pent ), sweepIndex++ );
}

static int Entity_Global( lua_State *L )
{
    lua_Number index = luaL_checknumber( L, 1 );
    if( !gpGlobals || !std::isfinite( (double)index ) || index < 0 ||
        index >= gpGlobals->maxEntities || std::floor( (double)index ) != index )
    {
        lua_pushnil( L );
        return 1;
    }
    XGLua_PushEntity( L, INDEXENT( (int)index ) );
    return 1;
}

static int Ent_GetGroundEntity( lua_State *L )
{
    edict_t *pent = XGLua_CheckEntity( L, 1 );
    XGLua_PushEntity( L, VARS( pent )->groundentity );
    return 1;
}

static bool XGLua_IsFiniteVector( const Vector &v )
{
    return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
}

static int Ent_SetPos( lua_State *L )
{
    edict_t *pent = XGLua_CheckEntity( L, 1 );
    Vector pos = XGLua_CheckVector( L, 2 );
    if( !XGLua_IsFiniteVector( pos ) )
        return luaL_argerror( L, 2, "position must be finite" );
    UTIL_SetOrigin( VARS( pent ), pos );
    return 0;
}

static int Ent_SetCollisionBounds( lua_State *L )
{
    edict_t *pent = XGLua_CheckEntity( L, 1 );
    Vector mins = XGLua_CheckVector( L, 2 );
    Vector maxs = XGLua_CheckVector( L, 3 );
    if( !XGLua_IsFiniteVector( mins ) || !XGLua_IsFiniteVector( maxs ) )
        return luaL_error( L, "collision bounds must be finite" );
    if( mins.x > maxs.x || mins.y > maxs.y || mins.z > maxs.z )
        return luaL_error( L, "collision mins must not exceed maxs" );
    UTIL_SetSize( VARS( pent ), mins, maxs );
    return 0;
}
static int ents_GetAll( lua_State *L )
{
	lua_newtable( L );
	int n = 0;
	for( int i = 0; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pent = INDEXENT( i );
		if( !pent || pent->free || !pent->pvPrivateData )
			continue;
		lua_pushnumber( L, ++n );
		XGLua_PushEntity( L, pent );
		lua_settable( L, -3 );
	}
	return 1;
}

static int ents_FindByClass( lua_State *L )
{
	const char *cls = luaL_checkstring( L, 1 );
	size_t len = strlen( cls );
	bool wildcard = len > 0 && cls[len - 1] == '*';

	lua_newtable( L );
	int n = 0;
	for( int i = 0; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pent = INDEXENT( i );
		if( !pent || pent->free || !pent->pvPrivateData )
			continue;
		const char *c = STRING( VARS( pent )->classname );
		if( !c || !c[0] )
			continue;
		if( wildcard )
		{
			if( strncmp( c, cls, len - 1 ) != 0 )
				continue;
		}
		else
		{
			if( strcmp( c, cls ) != 0 )
				continue;
		}
		lua_pushnumber( L, ++n );
		XGLua_PushEntity( L, pent );
		lua_settable( L, -3 );
	}
	return 1;
}

static int Ent_GetMaxHealth( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	lua_pushnumber( L, VARS( pent )->max_health );
	return 1;
}

static int Ent_Spawn( lua_State *L )
{
	edict_t *pent = XGLua_CheckEntity( L, 1 );
	if( ENTINDEX( pent ) == 0 ) return luaL_error( L, "Cannot spawn the world entity!" );
	DispatchSpawn( pent );
	return 0;
}

static int Ent_Remove( lua_State *L )
{
	if( ENTINDEX( XGLua_CheckEntity( L, 1 ) ) == 0 )
		return luaL_error( L, "Cannot remove the world entity!" );
	CBaseEntity *pEnt = CBaseEntity::Instance( XGLua_CheckEntity( L, 1 ) );
	if( pEnt )
		pEnt->SUB_Remove();	// deferred remove, engine-safe
	return 0;
}

#endif
