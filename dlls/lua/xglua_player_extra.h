#ifndef XGLUA_PLAYER_EXTRA_H
#define XGLUA_PLAYER_EXTRA_H

static int Ply_IsObserver( lua_State *L )
{
	 lua_pushboolean( L, Ply( L, 1 )->IsObserver() != 0 );
	 return 1;
}

static int Ply_GetFOV( lua_State *L )
{
	 lua_pushinteger( L, Ply( L, 1 )->m_iFOV );
	 return 1;
}

// GoldSrc uses zero to restore the configured default FOV. Player::UpdateClientData
// sends the SetFOV message and updates HUD/zoom sensitivity on the client.
static int Ply_SetFOV( lua_State *L )
{
    CBasePlayer *player = Ply( L, 1 );
    double value = luaL_checknumber( L, 2 );
    if( !std::isfinite( value ) || value < 0 || value > 179 || std::floor( value ) != value )
        return luaL_argerror( L, 2, "FOV must be an integer from 0 to 179" );
    player->pev->fov = player->m_iFOV = (int)value;
    return 0;
}

static int Ply_IsOnTrain( lua_State *L )
{
	 lua_pushboolean( L, ( Ply( L, 1 )->m_afPhysicsFlags & PFLAG_ONTRAIN ) != 0 );
	 return 1;
}

static int Ply_IsUsing( lua_State *L )
{
	 lua_pushboolean( L, ( Ply( L, 1 )->m_afPhysicsFlags & PFLAG_USING ) != 0 );
	 return 1;
}

static int Ply_InVehicle( lua_State *L )
{
	 lua_pushboolean( L, Ply( L, 1 )->m_pTank != NULL );
	 return 1;
}

static int Ply_IsFrozen( lua_State *L )
{
	 lua_pushboolean( L, ( Ply( L, 1 )->pev->flags & FL_FROZEN ) != 0 );
	 return 1;
}

static int Ply_IsGodMode( lua_State *L )
{
	 lua_pushboolean( L, ( Ply( L, 1 )->pev->flags & FL_GODMODE ) != 0 );
	 return 1;
}

#endif
