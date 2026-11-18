#ifndef XGLUA_ENTITY_QUERIES_H
#define XGLUA_ENTITY_QUERIES_H

#include <climits>

static bool XGLua_QueryEntityValid( const edict_t *ent )
{
	return ent && !ent->free && ent->pvPrivateData && !( ent->v.flags & FL_KILLME );
}

static void XGLua_AddQueryResult( lua_State *L, edict_t *ent, int &count )
{
	XGLua_PushEntity( L, ent );
	lua_rawseti( L, -2, ++count );
}

static int ents_FindByName( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TSTRING );
	size_t length;
	const char *name = lua_tolstring( L, 1, &length );
	if( memchr( name, '\0', length ) )
		return luaL_argerror( L, 1, "targetname contains a NUL byte" );
	lua_newtable( L );
	if( !length ) return 1;
	int count = 0;
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *ent = INDEXENT( i );
		if( !XGLua_QueryEntityValid( ent ) || FStringNull( ent->v.targetname ) ) continue;
		const char *targetname = STRING( ent->v.targetname );
		if( targetname && !strcmp( name, targetname ) ) XGLua_AddQueryResult( L, ent, count );
	}
	return 1;
}

static bool XGLua_OrderedBounds( const Vector &mins, const Vector &maxs )
{
	return mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z;
}

static double XGLua_DistanceToInterval( double value, double low, double high )
{
	if( value < low ) return low - value;
	if( value > high ) return value - high;
	return 0;
}

static int ents_FindInSphere( lua_State *L )
{
	const Vector center = XGLua_CheckVector( L, 1 );
	if( !XGLua_IsFiniteVector( center ) )
		return luaL_argerror( L, 1, "center must be finite" );
	luaL_checktype( L, 2, LUA_TNUMBER );
	const double radius = lua_tonumber( L, 2 );
	if( !std::isfinite( radius ) || radius < 0 )
		return luaL_argerror( L, 2, "radius must be finite and non-negative" );
	// All distances between finite float coordinates fit in a double even
	// after squaring. Cap the squared threshold without overflowing double.
	const double r = radius > 1e40 ? 1e40 : radius;
	const double radiusSquared = r * r;
	lua_newtable( L );
	int count = 0;
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *ent = INDEXENT( i );
		if( !XGLua_QueryEntityValid( ent ) ) continue;
		const Vector &lo = ent->v.absmin;
		const Vector &hi = ent->v.absmax;
		if( !XGLua_IsFiniteVector( lo ) || !XGLua_IsFiniteVector( hi ) || !XGLua_OrderedBounds( lo, hi ) ) continue;
		const double dx = XGLua_DistanceToInterval( center.x, lo.x, hi.x );
		const double dy = XGLua_DistanceToInterval( center.y, lo.y, hi.y );
		const double dz = XGLua_DistanceToInterval( center.z, lo.z, hi.z );
		if( dx * dx + dy * dy + dz * dz <= radiusSquared ) XGLua_AddQueryResult( L, ent, count );
	}
	return 1;
}

static int ents_FindInBox( lua_State *L )
{
	const Vector mins = XGLua_CheckVector( L, 1 );
	const Vector maxs = XGLua_CheckVector( L, 2 );
	if( !XGLua_IsFiniteVector( mins ) || !XGLua_IsFiniteVector( maxs ) )
		return luaL_error( L, "query bounds must be finite" );
	if( !XGLua_OrderedBounds( mins, maxs ) )
		return luaL_error( L, "query mins must not exceed maxs" );
	lua_newtable( L );
	int count = 0;
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *ent = INDEXENT( i );
		if( !XGLua_QueryEntityValid( ent ) ) continue;
		const Vector &lo = ent->v.absmin;
		const Vector &hi = ent->v.absmax;
		if( !XGLua_IsFiniteVector( lo ) || !XGLua_IsFiniteVector( hi ) || !XGLua_OrderedBounds( lo, hi ) ) continue;
		if( XGLua_OrderedBounds( mins, hi ) && XGLua_OrderedBounds( lo, maxs ) )
			XGLua_AddQueryResult( L, ent, count );
	}
	return 1;
}

static edict_t *XGLua_QueryPlayer( int index )
{
	edict_t *ent = INDEXENT( index );
	return XGLua_QueryEntityValid( ent ) && CBaseEntity::Instance( ent )->IsPlayer() ? ent : NULL;
}

static int XGLua_ListPlayers( lua_State *L, bool bots )
{
	lua_newtable( L );
	int count = 0;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *ent = XGLua_QueryPlayer( i );
		if( ent && ( ( ent->v.flags & FL_FAKECLIENT ) != 0 ) == bots )
			XGLua_AddQueryResult( L, ent, count );
	}
	return 1;
}

static int player_GetBots( lua_State *L ) { return XGLua_ListPlayers( L, true ); }
static int player_GetHumans( lua_State *L ) { return XGLua_ListPlayers( L, false ); }

static int XGLua_QueryUserID( edict_t *ent )
{
	return g_engfuncs.pfnGetPlayerUserId ? GETPLAYERUSERID( ent ) : -1;
}

static int Ply_UserID( lua_State *L )
{
	const int id = XGLua_QueryUserID( Ply( L, 1 )->edict() );
	if( id > 0 ) lua_pushinteger( L, id );
	else lua_pushnil( L );
	return 1;
}

static int player_GetByUserID( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TNUMBER );
	const double id = lua_tonumber( L, 1 );
	if( !std::isfinite( id ) || id < 1 || id > INT_MAX || std::floor( id ) != id )
		return luaL_argerror( L, 1, "user ID must be a positive integer" );
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *ent = XGLua_QueryPlayer( i );
		if( ent && XGLua_QueryUserID( ent ) == (int)id )
		{
			XGLua_PushEntity( L, ent );
			return 1;
		}
	}
	lua_pushnil( L );
	return 1;
}

static int game_GetMap( lua_State *L )
{
	lua_pushstring( L, FStringNull( gpGlobals->mapname ) ? "" : STRING( gpGlobals->mapname ) );
	return 1;
}

static int game_MaxPlayers( lua_State *L )
{
	lua_pushinteger( L, gpGlobals->maxClients );
	return 1;
}

static int game_GetWorld( lua_State *L )
{
	XGLua_PushEntity( L, INDEXENT( 0 ) );
	return 1;
}

#endif // XGLUA_ENTITY_QUERIES_H
