#ifndef XGLUA_ENTITY_METHODS_H
#define XGLUA_ENTITY_METHODS_H

static int Ent_Index( lua_State *L )
{
	// IsValid remains callable on a removed handle; all other access to a
	// stale entity fails explicitly, including instance fields and methods.
	XGLuaEntRef *ref = XGLua_TestEntityRef( L, 1 );
	if( !ref ) return luaL_error( L, "Expected an entity!" );
	edict_t *pent = XGLua_ValidateEnt( ref );
	if( !pent )
	{
		const char *key = lua_type( L, 2 ) == LUA_TSTRING ? lua_tostring( L, 2 ) : NULL;
		if( key && !strcmp( key, "IsValid" ) )
		{
			lua_pushcfunction( L, Ent_IsValid );
			return 1;
		}
		return luaL_error( L, "Tried to use a NULL entity!" );
	}
	XGLua_GetEntityField( L, 1, 2 );
	if( !lua_isnil( L, -1 ) ) return 1;
	lua_pop( L, 1 );
	const char *cls = XGLua_EntityClassName( pent );
	if( XGLua_IsLuaWeapon( cls ) )
	{
		XGLua_GetSWEPRegistry( L );
		lua_getfield( L, -1, cls );
		if( lua_istable( L, -1 ) )
		{
			lua_pushvalue( L, 2 );
			lua_gettable( L, -2 );
			if( !lua_isnil( L, -1 ) ) return 1;
			lua_pop( L, 1 );
		}
		lua_pop( L, 2 );
	}
	luaL_getmetatable( L, XGLUA_ENTITY_META );
	lua_pushvalue( L, 2 );
	lua_rawget( L, -2 );
	return 1;
}

static int Ent_NewIndex( lua_State *L )
{
	XGLua_CheckEntity( L, 1 );
	XGLua_SetEntityField( L, 1, 2, 3 );
	return 0;
}

#endif
