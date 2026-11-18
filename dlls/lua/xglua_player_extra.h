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
