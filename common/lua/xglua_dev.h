#ifndef XGLUA_DEV_H
#define XGLUA_DEV_H

// Shared M6 developer & diagnostics helpers:
//   - xgl_mem        : VM memory usage (KB + bytes)
//   - xgl_dump_hooks : list registered hook events & identifiers
//   - OnReloaded     : protected post-reload notification hook

#include <cstdio>
#include <cstring>
#include "lua/xglua_safe.h"
#include "lua/xglua_hook.h"

typedef void ( *XGLua_DevEmitFn )( const char *line );

// Formats Lua VM memory usage into out (e.g. "128.45 KB (131532 bytes)").
static bool XGLua_DevMemStats( lua_State *L, char *out, size_t outLen )
{
	if( !L || !out || outLen == 0 )
		return false;
	const int kb = lua_gc( L, LUA_GCCOUNT, 0 );
	const int rem = lua_gc( L, LUA_GCCOUNTB, 0 );
	const long long totalBytes = (long long)kb * 1024LL + (long long)rem;
	const double totalKB = (double)totalBytes / 1024.0;
	snprintf( out, outLen, "%.2f KB (%lld bytes)", totalKB, totalBytes );
	return true;
}

struct XGLua_DumpHooksCtx
{
	XGLua_DevEmitFn emit;
	int eventCount;
	int hookCount;
};

static int XGLua_DevDumpHooksCore( lua_State *L, void *ud )
{
	XGLua_DumpHooksCtx *ctx = (XGLua_DumpHooksCtx *)ud;
	ctx->eventCount = 0;
	ctx->hookCount = 0;

	lua_getglobal( L, "hook" );
	if( !lua_istable( L, -1 ) )
		return 0;
	lua_getfield( L, -1, "GetTable" );
	if( !lua_isfunction( L, -1 ) )
		return 0;
	if( lua_pcall( L, 0, 1, 0 ) != 0 )
		return lua_error( L );
	if( !lua_istable( L, -1 ) )
		return 0;

	const int tblIdx = lua_gettop( L );
	lua_pushnil( L );
	while( lua_next( L, tblIdx ) != 0 )
	{
		// [-2] event name, [-1] { [id] = fn }
		if( lua_type( L, -2 ) == LUA_TSTRING && lua_istable( L, -1 ) )
		{
			const char *evName = lua_tostring( L, -2 );
			const int evTbl = lua_gettop( L );
			int countInEvent = 0;

			char line[512];
			int pos = snprintf( line, sizeof( line ), "  %s:", evName );

			lua_pushnil( L );
			while( lua_next( L, evTbl ) != 0 )
			{
				if( lua_isfunction( L, -1 ) )
				{
					++countInEvent;
					++ctx->hookCount;
					const char *idStr = ( lua_type( L, -2 ) == LUA_TSTRING )
						? lua_tostring( L, -2 )
						: lua_typename( L, lua_type( L, -2 ) );
					if( pos > 0 && (size_t)pos < sizeof( line ) - 4 )
					{
						pos += snprintf( line + pos, sizeof( line ) - (size_t)pos,
							countInEvent == 1 ? " %s" : ", %s", idStr );
					}
				}
				lua_pop( L, 1 );
			}

			if( countInEvent > 0 )
			{
				++ctx->eventCount;
				if( ctx->emit )
					ctx->emit( line );
			}
		}
		lua_pop( L, 1 );
	}
	return 0;
}

static void XGLua_DevDumpHooks( lua_State *L, XGLua_DevEmitFn emit,
	void ( *onerror )( lua_State *, const char * ), const char *tag )
{
	if( !L || !emit )
		return;
	XGLua_DumpHooksCtx ctx = { emit, 0, 0 };
	XGLua_ProtectedCall( L, XGLua_DevDumpHooksCore, &ctx, onerror, "xgl_dump_hooks" );

	char summary[128];
	snprintf( summary, sizeof( summary ), "[%s] %d hook(s) across %d event(s)",
		tag ? tag : "XGLua", ctx.hookCount, ctx.eventCount );
	emit( summary );
}

static void XGLua_DevTriggerReloaded( lua_State *L, void ( *onerror )( lua_State *, const char * ) )
{
	if( !L )
		return;
	XGLua_HookRunProtected( L, "OnReloaded", NULL, NULL, onerror );
}

#endif // XGLUA_DEV_H
