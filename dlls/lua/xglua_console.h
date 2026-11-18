#ifndef XGLUA_CONSOLE_H
#define XGLUA_CONSOLE_H

static char xgluaConsoleKey;

static bool XGLua_ConsoleName( const char *name, size_t len )
{
	if( !name || !len || len > 63 ) return false;
	for( size_t i = 0; i < len; ++i )
		if( !( name[i] >= 'a' && name[i] <= 'z' ) && !( name[i] >= '0' && name[i] <= '9' ) && name[i] != '_' ) return false;
	return true;
}

static const char *XGLua_CheckConsoleName( lua_State *L )
{
	luaL_checktype( L, 1, LUA_TSTRING );
	size_t len;
	const char *name = lua_tolstring( L, 1, &len );
	if( !XGLua_ConsoleName( name, len ) ) luaL_argerror( L, 1, "expected 1-63 lowercase letters, digits or underscores" );
	return name;
}

static void XGLua_PushConsoleCommands( lua_State *L )
{
	lua_pushlightuserdata( L, &xgluaConsoleKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
}

static int XGLua_ConsoleAdd( lua_State *L )
{
	const char *name = XGLua_CheckConsoleName( L );
	luaL_checktype( L, 2, LUA_TFUNCTION );
	XGLua_PushConsoleCommands( L );
	lua_getfield( L, -1, name );
	const bool exists = !lua_isnil( L, -1 );
	lua_pop( L, 1 );
	if( !exists )
	{
		int count = 0;
		lua_pushnil( L );
		while( lua_next( L, -2 ) )
		{
			if( ++count >= 128 ) return luaL_error( L, "Lua command limit reached (128)" );
			lua_pop( L, 1 );
		}
	}
	lua_pushvalue( L, 2 );
	lua_setfield( L, -2, name );
	return 0;
}

static int XGLua_ConsoleRemove( lua_State *L )
{
	const char *name = XGLua_CheckConsoleName( L );
	XGLua_PushConsoleCommands( L );
	lua_pushnil( L );
	lua_setfield( L, -2, name );
	return 0;
}

struct XGLuaConsoleInvocation { char name[64]; int argc; };

static int XGLua_ConsoleDispatch( lua_State *L, void *context )
{
	const XGLuaConsoleInvocation *cmd = (const XGLuaConsoleInvocation *)context;
	XGLua_PushConsoleCommands( L );
	lua_getfield( L, -1, cmd->name );
	if( !lua_isfunction( L, -1 ) )
	{
		ALERT( at_console, "[XGLua] unknown Lua command: %s\n", cmd->name );
		return 0;
	}
	lua_pushnil( L );
	lua_pushstring( L, cmd->name );
	lua_createtable( L, cmd->argc - 2, 0 );
	for( int i = 2; i < cmd->argc; ++i )
	{
		lua_pushstring( L, CMD_ARGV( i ) );
		lua_rawseti( L, -2, i - 1 );
	}
	// Join tokens; engine tokenization has already discarded original quoting.
	luaL_Buffer buf;
	luaL_buffinit( L, &buf );
	for( int i = 2; i < cmd->argc; ++i )
	{
		if( i > 2 ) luaL_addchar( &buf, ' ' );
		luaL_addstring( &buf, CMD_ARGV( i ) );
	}
	luaL_pushresult( &buf );
	lua_call( L, 4, 0 );
	return 0;
}

static void XGLua_LuaCmd_f()
{
	if( !g_xglua_L ) return;
	XGLuaConsoleInvocation cmd = {};
	cmd.argc = CMD_ARGC();
	const char *name = cmd.argc >= 2 ? CMD_ARGV( 1 ) : NULL;
	if( !name || !XGLua_ConsoleName( name, strlen( name ) ) || cmd.argc > 66 )
	{
		ALERT( at_console, "usage: lua_cmd <name> [up to 64 arguments]\n" );
		return;
	}
	strcpy( cmd.name, name );
	XGLua_ProtectedCall( g_xglua_L, XGLua_ConsoleDispatch, &cmd, XGLua_ReportError, "lua_cmd" );
}

#define XGLUA_CONVAR_META "XGLua_ConVar"
struct XGLuaConVarSnapshot { char name[64]; double value; };

static const XGLuaConVarSnapshot *XGLua_CheckConVar( lua_State *L )
{
	return (const XGLuaConVarSnapshot *)luaL_checkudata( L, 1, XGLUA_CONVAR_META );
}

static int XGLua_ConVarName( lua_State *L ) { lua_pushstring( L, XGLua_CheckConVar( L )->name ); return 1; }
static int XGLua_ConVarString( lua_State *L ) { lua_pushstring( L, (const char *)( XGLua_CheckConVar( L ) + 1 ) ); return 1; }
static int XGLua_ConVarFloat( lua_State *L ) { lua_pushnumber( L, XGLua_CheckConVar( L )->value ); return 1; }
static int XGLua_ConVarInt( lua_State *L ) { lua_pushnumber( L, std::trunc( XGLua_CheckConVar( L )->value ) ); return 1; }
static int XGLua_ConVarBool( lua_State *L ) { lua_pushboolean( L, XGLua_CheckConVar( L )->value != 0 ); return 1; }

static int XGLua_GetConVar( lua_State *L )
{
	const char *name = XGLua_CheckConsoleName( L );
	static const char *const allowed[] = { "deathmatch", "developer", "mp_friendlyfire", "skill", "sv_gravity", "sv_maxspeed", "xgl_gamemode" };
	bool readable = false;
	for( const char *entry : allowed ) if( !strcmp( name, entry ) ) { readable = true; break; }
	const cvar_t *var = readable && g_engfuncs.pfnCVarGetPointer ? g_engfuncs.pfnCVarGetPointer( name ) : NULL;
	if( !var || ( var->flags & ( FCVAR_PROTECTED | FCVAR_PRIVILEGED | FCVAR_USERINFO ) ) )
	{
		lua_pushnil( L );
		return 1;
	}
	// Keep the snapshot independent of engine-owned strings and cvar storage.
	char value[4097];
	const char *string = var->string ? var->string : "";
	const size_t length = strlen( string );
	if( length >= sizeof( value ) ) return luaL_error( L, "ConVar value exceeds 4096 bytes" );
	memcpy( value, string, length + 1 );
	const double number = std::isfinite( var->value ) ? var->value : 0;
	XGLuaConVarSnapshot *snap = (XGLuaConVarSnapshot *)lua_newuserdata( L, sizeof( *snap ) + length + 1 );
	strcpy( snap->name, name );
	snap->value = number;
	memcpy( snap + 1, value, length + 1 );
	luaL_getmetatable( L, XGLUA_CONVAR_META );
	lua_setmetatable( L, -2 );
	return 1;
}

static void XGLua_RegisterConsole( lua_State *L )
{
	lua_pushlightuserdata( L, &xgluaConsoleKey );
	lua_newtable( L );
	lua_rawset( L, LUA_REGISTRYINDEX );
	const luaL_Reg commands[] = { { "Add", XGLua_ConsoleAdd }, { "Remove", XGLua_ConsoleRemove }, { NULL, NULL } };
	luaL_register( L, "concommand", commands );
	lua_pop( L, 1 );
	const luaL_Reg convars[] = {
		{ "GetName", XGLua_ConVarName }, { "GetString", XGLua_ConVarString },
		{ "GetFloat", XGLua_ConVarFloat }, { "GetInt", XGLua_ConVarInt },
		{ "GetBool", XGLua_ConVarBool }, { NULL, NULL }
	};
	luaL_newmetatable( L, XGLUA_CONVAR_META );
	lua_newtable( L );
	luaL_register( L, NULL, convars );
	lua_setfield( L, -2, "__index" );
	lua_pushliteral( L, "ConVar" );
	lua_setfield( L, -2, "__metatable" );
	lua_pop( L, 1 );
	lua_pushcfunction( L, XGLua_GetConVar );
	lua_setglobal( L, "GetConVar" );
}

#endif // XGLUA_CONSOLE_H
