#ifndef XGLUA_ENTITY_STATE_H
#define XGLUA_ENTITY_STATE_H

static int Ent_GetMoveType( lua_State *L )
{
	lua_pushinteger( L, VARS( XGLua_CheckEntity( L, 1 ) )->movetype );
	return 1;
}

static int Ent_GetSolid( lua_State *L )
{
	lua_pushinteger( L, VARS( XGLua_CheckEntity( L, 1 ) )->solid );
	return 1;
}

static int Ent_WaterLevel( lua_State *L )
{
	lua_pushinteger( L, VARS( XGLua_CheckEntity( L, 1 ) )->waterlevel );
	return 1;
}

static int Ent_OnGround( lua_State *L )
{
	lua_pushboolean( L, ( VARS( XGLua_CheckEntity( L, 1 ) )->flags & FL_ONGROUND ) != 0 );
	return 1;
}

static int Ply_Crouching( lua_State *L )
{
	lua_pushboolean( L, ( Ply( L, 1 )->pev->flags & FL_DUCKING ) != 0 );
	return 1;
}

static int Ply_Armor( lua_State *L )
{
	lua_pushnumber( L, Ply( L, 1 )->pev->armorvalue );
	return 1;
}

static int Ply_Ping( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	int ping = 0;
	int loss = 0;
	if( g_engfuncs.pfnGetPlayerStats )
		PLAYER_CNX_STATS( player->edict(), &ping, &loss );
	lua_pushinteger( L, ping );
	return 1;
}

static unsigned int XGLua_CheckButtonMask( lua_State *L )
{
	luaL_checktype( L, 2, LUA_TNUMBER );
	const double mask = lua_tonumber( L, 2 );
	if( !std::isfinite( mask ) || mask < 0 || mask > 65535 || std::floor( mask ) != mask )
		luaL_argerror( L, 2, "expected a 16-bit unsigned integer button mask" );
	return (unsigned int)mask;
}

static int Ply_KeyDown( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	lua_pushboolean( L, ( (unsigned int)player->pev->button & XGLua_CheckButtonMask( L ) ) != 0 );
	return 1;
}

static int Ply_KeyPressed( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	lua_pushboolean( L, ( (unsigned int)player->m_afButtonPressed & XGLua_CheckButtonMask( L ) ) != 0 );
	return 1;
}

static int Ply_KeyReleased( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	lua_pushboolean( L, ( (unsigned int)player->m_afButtonReleased & XGLua_CheckButtonMask( L ) ) != 0 );
	return 1;
}

#endif // XGLUA_ENTITY_STATE_H
