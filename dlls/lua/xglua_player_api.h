#ifndef XGLUA_PLAYER_API_H
#define XGLUA_PLAYER_API_H

static int Ply_Name( lua_State *L )
{
	lua_pushstring( L, STRING( VARS( XGLua_CheckEntity( L, 1 ) )->netname ) );
	return 1;
}

static int Ply_Frags( lua_State *L )
{
	lua_pushnumber( L, VARS( Ply( L, 1 )->edict() )->frags );
	return 1;
}

static int Ply_Deaths( lua_State *L )
{
	lua_pushinteger( L, Ply( L, 1 )->m_iDeaths );
	return 1;
}

static int Ply_Team( lua_State *L )
{
	const char *team = Ply( L, 1 )->TeamID();
	lua_pushstring( L, team ? team : "" );
	return 1;
}

static int Ply_IsBot( lua_State *L )
{
	lua_pushboolean( L, ( VARS( Ply( L, 1 )->edict() )->flags & FL_FAKECLIENT ) != 0 );
	return 1;
}

static int Ply_IsConnected( lua_State *L )
{
	edict_t *ent = XGLua_CheckEntity( L, 1 );
	lua_pushboolean( L, !ent->free && ent->pvPrivateData && ENTINDEX( ent ) > 0 &&
		ENTINDEX( ent ) <= gpGlobals->maxClients );
	return 1;
}

static int Ply_HasWeapons( lua_State *L )
{
	lua_pushboolean( L, Ply( L, 1 )->HasWeapons() != FALSE );
	return 1;
}

static int Ply_GetWeapons( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	lua_newtable( L );
	int count = 0;
	for( int slot = 0; slot < MAX_ITEM_TYPES; ++slot )
	{
		for( CBasePlayerItem *item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext )
		{
			XGLua_PushEntity( L, item->edict() );
			lua_rawseti( L, -2, ++count );
		}
	}
	return 1;
}

static int Ply_HasWeapon( lua_State *L )
{
	CBasePlayer *player = Ply( L, 1 );
	const char *classname = luaL_checkstring( L, 2 );
	for( int slot = 0; slot < MAX_ITEM_TYPES; ++slot )
		for( CBasePlayerItem *item = player->m_rgpPlayerItems[slot]; item; item = item->m_pNext )
			if( FClassnameIs( item->pev, classname ) )
			{
				lua_pushboolean( L, 1 );
				return 1;
			}
	lua_pushboolean( L, 0 );
	return 1;
}

#endif
