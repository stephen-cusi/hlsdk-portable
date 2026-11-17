#ifndef XGLUA_GAMEMODE_H
#define XGLUA_GAMEMODE_H

// Server gamemode loading chain (design doc 3.2 / 4.2 / 9.7, M4 first batch).
//
// Selection comes from the xgl_gamemode cvar. The chain is resolved from the
// KeyValues metadata gamemodes/<name>/<name>.txt ("base" key), then executed
// base-first: for each mode a fresh GM table is installed as the global GM
// (with GM.BaseClass pointing at the previously loaded mode's table), the
// scripts run as shared.lua then init.lua, and the finished table is handed
// to the Lua-side gamemode.Register. The selected mode's table becomes the
// global GAMEMODE, which the existing hook.Run gamemode fallback reads.
//
// Failure policy: no selection, or an empty gamemodes tree, keeps the
// current startup behavior; any failure while loading (bad name, missing
// metadata or script, parse error, base cycle, script error) is reported
// through errbuf, GM/GAMEMODE are cleared, and the server continues without
// a gamemode. Nothing here aborts the VM.
//
// Every Lua touch runs inside the shared protected boundary (xglua_safe.h):
// global reads/writes honor _G metamethods and gamemode scripts are user
// code. The error-capture buffer is static like the SWEP cache one: the
// loader is single-threaded startup code and never reentrant.
//
// Include after xglua.h (lua.h extern "C") and xglua_vm_include.h
// (XGLuaReadScript typedef); needs lauxlib via xglua_safe.h.

#include "xglua_safe.h"
#include "xglua_vm_include.h"
#include <cstring>

#define XGLUA_GM_NAME_MAX 32
#define XGLUA_GM_MAX_CHAIN 16
#define XGLUA_GM_TOKEN_MAX 511

enum XGLua_GMResult
{
	XGLUA_GM_OK = 0,
	XGLUA_GM_NO_SELECTION,
	XGLUA_GM_BAD_NAME,
	XGLUA_GM_MISSING_META,
	XGLUA_GM_BAD_META,
	XGLUA_GM_MISSING_SCRIPT,
	XGLUA_GM_READ_ERROR,
	XGLUA_GM_SCRIPT_ERROR,
	XGLUA_GM_CYCLE,
	XGLUA_GM_TOO_DEEP,
	XGLUA_GM_NO_MODULE,
	XGLUA_GM_INTERNAL
};

// Gamemode names become path components: restricted charset, no separators,
// no dots, bounded length. This is the traversal guard.
static bool XGLua_GMValidName( const char *name )
{
	if( !name || !*name )
		return false;
	size_t n = std::strlen( name );
	if( n > XGLUA_GM_NAME_MAX )
		return false;
	for( size_t i = 0; i < n; ++i )
	{
		char c = name[i];
		if( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) )
			return false;
	}
	return true;
}

static void XGLua_GMNormalizeName( char *out, size_t outlen, const char *name )
{
	size_t i = 0;
	if( !outlen ) return;
	for( ; name && name[i] && i + 1 < outlen; ++i )
	{
		char c = name[i];
		out[i] = ( c >= 'A' && c <= 'Z' ) ? (char)( c + ( 'a' - 'A' ) ) : c;
	}
	out[i] = '\0';
}

//===========================================================================
// KeyValues metadata: extract "base" from gamemodes/<name>/<name>.txt
//===========================================================================

struct XGLua_GMKVScanner
{
	const char *p;
	const char *end;
	int line;
};

static void XGLua_GMKVSkipSpace( XGLua_GMKVScanner *s )
{
	for( ; s->p < s->end; )
	{
		char c = *s->p;
		if( c == '\n' ) { ++s->line; ++s->p; }
		else if( c == ' ' || c == '\t' || c == '\r' ) ++s->p;
		else if( c == '/' && s->p + 1 < s->end && s->p[1] == '/' )
		{
			while( s->p < s->end && *s->p != '\n' ) ++s->p;
		}
		else break;
	}
}

// Read one double-quoted token. Returns false at clean end of input; on a
// syntax problem *bad is set with the scanner left at the offending line.
static bool XGLua_GMKVToken( XGLua_GMKVScanner *s, char *out, size_t outlen, bool *bad )
{
	XGLua_GMKVSkipSpace( s );
	if( s->p >= s->end )
		return false;
	if( *s->p != '"' )
	{
		*bad = true;
		return false;
	}
	++s->p;
	size_t n = 0;
	while( s->p < s->end && *s->p != '"' )
	{
		char c = *s->p;
		if( c == '\n' ) { *bad = true; return false; }	// unterminated quote
		if( c == '\\' && s->p + 1 < s->end && ( s->p[1] == '"' || s->p[1] == '\\' ) )
			c = *++s->p;
		if( n + 1 >= outlen ) { *bad = true; return false; }
		out[n++] = c;
		++s->p;
	}
	if( s->p >= s->end ) { *bad = true; return false; }
	++s->p;	// closing quote
	out[n] = '\0';
	return true;
}

static bool XGLua_GMKVExpect( XGLua_GMKVScanner *s, char c, bool *bad )
{
	XGLua_GMKVSkipSpace( s );
	if( s->p >= s->end || *s->p != c ) { *bad = true; return false; }
	++s->p;
	return true;
}

// out receives the base name (empty string when the metadata declares no
// base). Only the first "base" key is honored, matching GMod behavior.
static bool XGLua_GMParseBase( const char *data, size_t len, char *out, size_t outlen,
	int *errLine )
{
	out[0] = '\0';
	*errLine = 0;
	if( len >= 3 && (unsigned char)data[0] == 0xef && (unsigned char)data[1] == 0xbb &&
		(unsigned char)data[2] == 0xbf )
	{
		data += 3;
		len -= 3;
	}
	XGLua_GMKVScanner s = { data, data + len, 1 };
	bool bad = false;
	bool baseSeen = false;
	char root[XGLUA_GM_TOKEN_MAX + 1];
	char key[XGLUA_GM_TOKEN_MAX + 1], value[XGLUA_GM_TOKEN_MAX + 1];

	if( !XGLua_GMKVToken( &s, root, sizeof( root ), &bad ) ) { *errLine = s.line; return false; }
	if( !XGLua_GMKVExpect( &s, '{', &bad ) ) { *errLine = s.line; return false; }
	for( ;; )
	{
		XGLua_GMKVSkipSpace( &s );
		if( s.p >= s.end )
		{
			*errLine = s.line;
			return false;	// EOF before '}'
		}
		if( *s.p == '}' )
		{
			++s.p;
			XGLua_GMKVSkipSpace( &s );
			if( s.p != s.end ) { *errLine = s.line; return false; }
			return true;	// empty block or complete root block
		}
		if( !XGLua_GMKVToken( &s, key, sizeof( key ), &bad ) ) { *errLine = s.line; return false; }
		if( !XGLua_GMKVToken( &s, value, sizeof( value ), &bad ) ) { *errLine = s.line; return false; }
		if( !baseSeen && std::strcmp( key, "base" ) == 0 )
		{
			baseSeen = true;
			if( std::strlen( value ) >= outlen ) { *errLine = s.line; return false; }
			std::snprintf( out, outlen, "%s", value );
		}
	}
}

//===========================================================================
// protected Lua steps
//===========================================================================

// Static capture buffer (non-reentrant startup path), like the SWEP cache's
// g_szSWEPFieldError: the boundary's onerror callback has no context slot.
static char g_szGMLuaError[192];
static void XGLua_GMCaptureError( lua_State *L, const char *context )
{
	(void)context;
	std::snprintf( g_szGMLuaError, sizeof( g_szGMLuaError ), "%s", XGLua_ErrorText( L, -1 ) );
	lua_pop( L, 1 );
}

// Install a fresh GM table; non-root modes get GM.BaseClass = previous mode.
struct XGLua_GMModeCtx { int baseRef; };
static int XGLua_GMModeStartCore( lua_State *L, void *ud )
{
	XGLua_GMModeCtx *mc = (XGLua_GMModeCtx *)ud;
	lua_newtable( L );
	if( mc->baseRef != LUA_NOREF )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, mc->baseRef );
		lua_setfield( L, -2, "BaseClass" );
	}
	lua_pushvalue( L, -1 );
	lua_setglobal( L, "GAMEMODE" );
	lua_setglobal( L, "GM" );
	return 0;
}

// Run one already-read script (chunk name "@gamemodes/..."). luaL_loadbuffer
// is protected internally; the pcall error propagates to the outer frame.
struct XGLua_GMScriptCtx { const char *data; size_t len; const char *chunk; };
static int XGLua_GMRunScriptCore( lua_State *L, void *ud )
{
	XGLua_GMScriptCtx *sc = (XGLua_GMScriptCtx *)ud;
	if( luaL_loadbuffer( L, sc->data, sc->len, sc->chunk ) != 0 )
		return lua_error( L );
	if( lua_pcall( L, 0, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

// gamemode.Register( GM, name ) through the real Lua module.
struct XGLua_GMRegisterCtx { const char *name; };
static int XGLua_GMRegisterCore( lua_State *L, void *ud )
{
	XGLua_GMRegisterCtx *rc = (XGLua_GMRegisterCtx *)ud;
	lua_getglobal( L, "gamemode" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushliteral( L, "gamemode module not loaded" );
		return lua_error( L );
	}
	lua_getfield( L, -1, "Register" );
	if( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		lua_pushliteral( L, "gamemode.Register missing" );
		return lua_error( L );
	}
	lua_remove( L, -2 );
	lua_getglobal( L, "GM" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushliteral( L, "GM global is not a table" );
		return lua_error( L );
	}
	lua_pushstring( L, rc->name );
	if( lua_pcall( L, 2, 0, 0 ) != 0 )
		return lua_error( L );
	return 0;
}

// Snapshot the current GM table into a registry reference (BaseClass source
// for the next mode; also proves the global survived the scripts).
struct XGLua_GMRefCtx { int ref; };
static int XGLua_GMRefCore( lua_State *L, void *ud )
{
	XGLua_GMRefCtx *rc = (XGLua_GMRefCtx *)ud;
	lua_getglobal( L, "GM" );
	if( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushliteral( L, "GM global is not a table" );
		return lua_error( L );
	}
	rc->ref = luaL_ref( L, LUA_REGISTRYINDEX );
	return 0;
}

struct XGLua_GMUnrefCtx { int *refs; int count; };
static int XGLua_GMUnrefCore( lua_State *L, void *ud )
{
	XGLua_GMUnrefCtx *uc = (XGLua_GMUnrefCtx *)ud;
	for( int i = 0; i < uc->count; ++i )
	{
		if( uc->refs[i] != LUA_NOREF )
		{
			luaL_unref( L, LUA_REGISTRYINDEX, uc->refs[i] );
			uc->refs[i] = LUA_NOREF;
		}
	}
	return 0;
}

// GAMEMODE = GM; also used with nil to clear both globals on failure.
static int XGLua_GMFinalizeCore( lua_State *L, void *ud )
{
	(void)ud;
	lua_getglobal( L, "GM" );
	lua_setglobal( L, "GAMEMODE" );
	return 0;
}

static int XGLua_GMClearCore( lua_State *L, void *ud )
{
	(void)ud;
	lua_pushnil( L );
	lua_setglobal( L, "GM" );
	lua_pushnil( L );
	lua_setglobal( L, "GAMEMODE" );
	return 0;
}

static XGLua_GMResult XGLua_GMAbort( lua_State *L, XGLua_GMResult result )
{
	if( L ) XGLua_ProtectedCall( L, XGLua_GMClearCore, NULL, NULL, "gamemode-clear" );
	return result;
}

//===========================================================================
// disk reader rooted at <gamedir> (gamemodes/ is a sibling of lua/)
//===========================================================================

static XGLuaReadStatus XGLua_ReadDiskRoot( void *ctx, const char *rel, char **out, size_t *len )
{
	char path[512];
	int n = std::snprintf( path, sizeof( path ), "%s/%s", (const char *)ctx, rel );
	if( n < 0 || (size_t)n >= sizeof( path ) ) return XGLUA_READ_ERROR;
	errno = 0;
	FILE *fp = std::fopen( path, "rb" );
	if( !fp ) return errno == ENOENT ? XGLUA_READ_MISSING : XGLUA_READ_ERROR;
	if( std::fseek( fp, 0, SEEK_END ) != 0 ) { std::fclose( fp ); return XGLUA_READ_ERROR; }
	long size = std::ftell( fp );
	if( size < 0 || std::fseek( fp, 0, SEEK_SET ) != 0 ) { std::fclose( fp ); return XGLUA_READ_ERROR; }
	char *data = (char *)std::malloc( (size_t)size + 1 );
	if( !data ) { std::fclose( fp ); return XGLUA_READ_ERROR; }
	size_t got = std::fread( data, 1, (size_t)size, fp );
	bool bad = ( got != (size_t)size || std::ferror( fp ) );
	if( std::fclose( fp ) != 0 ) bad = true;
	if( bad ) { std::free( data ); return XGLUA_READ_ERROR; }
	data[got] = '\0';
	*out = data;
	*len = got;
	return XGLUA_READ_OK;
}

//===========================================================================
// the chain loader
//===========================================================================

struct XGLua_GMLoadIO
{
	XGLuaReadScript read;
	void *ctx;
	int reads;		// diagnostics: how many files the loader touched
};

// Read one file; returns NULL on failure with a diagnostic in err.
static char *XGLua_GMReadFile( XGLua_GMLoadIO *io, const char *rel, size_t *len,
	char *err, size_t errlen, XGLua_GMResult *code )
{
	char *data = NULL;
	*len = 0;
	++io->reads;
	XGLuaReadStatus st = io->read( io->ctx, rel, &data, len );
	if( st == XGLUA_READ_OK )
		return data;
	*code = ( st == XGLUA_READ_MISSING ) ? XGLUA_GM_MISSING_META : XGLUA_GM_READ_ERROR;
	std::snprintf( err, errlen, "%s: %s", st == XGLUA_READ_MISSING ? "missing" : "unreadable", rel );
	return NULL;
}

// Resolve + execute. Returns XGLUA_GM_OK with the selected mode still in GM
// and mirrored into GAMEMODE, or a failure code with a diagnostic and both
// globals cleared.
static XGLua_GMResult XGLua_LoadGamemodeChain( lua_State *L, XGLuaReadScript read, void *readCtx,
	const char *requested, char *err, size_t errlen )
{
	if( err && errlen ) err[0] = '\0';
	if( !L )
		return XGLUA_GM_INTERNAL;
	if( !requested || !*requested )
		return XGLUA_GM_NO_SELECTION;
	if( !read )
	{
		std::snprintf( err, errlen, "gamemode reader is null" );
		return XGLua_GMAbort( L, XGLUA_GM_INTERNAL );
	}

	if( !XGLua_GMValidName( requested ) )
	{
		std::snprintf( err, errlen, "invalid gamemode name '%.32s'", requested ? requested : "" );
		return XGLua_GMAbort( L, XGLUA_GM_BAD_NAME );
	}

	XGLua_GMLoadIO io = { read, readCtx, 0 };

	// ---- resolve the chain: requested -> ... -> root, then reverse -------
	char names[XGLUA_GM_MAX_CHAIN][XGLUA_GM_NAME_MAX + 1];
	int count = 0;
	XGLua_GMNormalizeName( names[count], sizeof( names[count] ), requested );
	++count;

	while( true )
	{
		const char *cur = names[count - 1];
		char rel[128];
		std::snprintf( rel, sizeof( rel ), "gamemodes/%s/%s.txt", cur, cur );
		size_t len = 0;
		XGLua_GMResult code = XGLUA_GM_OK;
		char *data = XGLua_GMReadFile( &io, rel, &len, err, errlen, &code );
		if( !data )
		{
			if( count > 1 )
				std::snprintf( err, errlen, "base gamemode '%s' of '%s' not found (%s)",
					cur, names[count - 2], rel );
			return XGLua_GMAbort( L, code );
		}

		char base[XGLUA_GM_NAME_MAX + 1];
		int errLine = 0;
		bool parsed = XGLua_GMParseBase( data, len, base, sizeof( base ), &errLine );
		std::free( data );
		if( !parsed )
		{
			std::snprintf( err, errlen, "malformed metadata %s near line %d", rel, errLine );
			return XGLua_GMAbort( L, XGLUA_GM_BAD_META );
		}
		if( !base[0] )
			break;	// root reached

		if( !XGLua_GMValidName( base ) )
		{
			std::snprintf( err, errlen, "invalid base name '%.32s' in %s", base, rel );
			return XGLua_GMAbort( L, XGLUA_GM_BAD_NAME );
		}
		char normalizedBase[XGLUA_GM_NAME_MAX + 1];
		XGLua_GMNormalizeName( normalizedBase, sizeof( normalizedBase ), base );
		if( count >= XGLUA_GM_MAX_CHAIN )
		{
			std::snprintf( err, errlen, "base chain deeper than %d at '%s'", XGLUA_GM_MAX_CHAIN, base );
			return XGLua_GMAbort( L, XGLUA_GM_TOO_DEEP );
		}
		for( int i = 0; i < count; ++i )
		{
			if( std::strcmp( names[i], normalizedBase ) == 0 )
			{
				std::snprintf( err, errlen, "base cycle: '%s' derives from '%s'", cur, base );
				return XGLua_GMAbort( L, XGLUA_GM_CYCLE );
			}
		}
		std::snprintf( names[count], sizeof( names[count] ), "%s", normalizedBase );
		++count;
	}

	// ---- execute root-first; every Lua touch is protected ---------------
	int refs[XGLUA_GM_MAX_CHAIN];
	for( int i = 0; i < XGLUA_GM_MAX_CHAIN; ++i ) refs[i] = LUA_NOREF;
	XGLua_GMResult fail = XGLUA_GM_OK;

	for( int i = count - 1; i >= 0 && !fail; --i )
	{
		const char *mode = names[i];
		XGLua_GMModeCtx mc = { ( i == count - 1 ) ? LUA_NOREF : refs[i + 1] };
		g_szGMLuaError[0] = '\0';
		if( !XGLua_ProtectedCall( L, XGLua_GMModeStartCore, &mc, NULL, mode ) )
		{
			std::snprintf( err, errlen, "%s: GM install failed", mode );
			fail = XGLUA_GM_INTERNAL;
			break;
		}

		static const char *const scripts[2] = { "shared.lua", "init.lua" };
		for( int s = 0; s < 2 && !fail; ++s )
		{
			char rel[160];
			std::snprintf( rel, sizeof( rel ), "gamemodes/%s/gamemode/%s", mode, scripts[s] );
			size_t len = 0;
			XGLua_GMResult code = XGLUA_GM_OK;
			char *data = XGLua_GMReadFile( &io, rel, &len, err, errlen, &code );
			if( !data )
			{
				// GMReadFile already wrote "missing/unreadable: <rel>"
				fail = ( code == XGLUA_GM_READ_ERROR ) ? XGLUA_GM_READ_ERROR : XGLUA_GM_MISSING_SCRIPT;
				break;
			}
			char chunk[192];
			std::snprintf( chunk, sizeof( chunk ), "@%s", rel );
			XGLua_GMScriptCtx sc = { data, len, chunk };
			g_szGMLuaError[0] = '\0';
			bool ok = XGLua_ProtectedCall( L, XGLua_GMRunScriptCore, &sc, XGLua_GMCaptureError, chunk );
			std::free( data );
			if( !ok )
			{
				std::snprintf( err, errlen, "%s: %s: %s", mode, rel,
					g_szGMLuaError[0] ? g_szGMLuaError : "script error" );
				fail = XGLUA_GM_SCRIPT_ERROR;
				break;
			}
		}
		if( fail )
			break;

		XGLua_GMRegisterCtx rc = { mode };
		g_szGMLuaError[0] = '\0';
		if( !XGLua_ProtectedCall( L, XGLua_GMRegisterCore, &rc, XGLua_GMCaptureError, mode ) )
		{
			std::snprintf( err, errlen, "%s: register failed: %s", mode,
				g_szGMLuaError[0] ? g_szGMLuaError : "unknown error" );
			// a missing module is an integration problem, not a script error
			fail = std::strstr( g_szGMLuaError, "gamemode module not loaded" ) ||
				std::strstr( g_szGMLuaError, "gamemode.Register missing" )
				? XGLUA_GM_NO_MODULE : XGLUA_GM_SCRIPT_ERROR;
			break;
		}

		XGLua_GMRefCtx refc = { LUA_NOREF };
		if( !XGLua_ProtectedCall( L, XGLua_GMRefCore, &refc, NULL, mode ) || refc.ref == LUA_NOREF )
		{
			std::snprintf( err, errlen, "%s: GM snapshot failed", mode );
			fail = XGLUA_GM_INTERNAL;
			break;
		}
		refs[i] = refc.ref;
	}

	// ---- finalize or clean up --------------------------------------------
	XGLua_GMUnrefCtx uc = { refs, XGLUA_GM_MAX_CHAIN };
	g_szGMLuaError[0] = '\0';
	if( !XGLua_ProtectedCall( L, XGLua_GMUnrefCore, &uc, XGLua_GMCaptureError, "gamemode cleanup" ) && !fail )
	{
		std::snprintf( err, errlen, "gamemode registry cleanup failed: %s",
			g_szGMLuaError[0] ? g_szGMLuaError : "unknown error" );
		fail = XGLUA_GM_INTERNAL;
	}

	if( fail )
	{
		XGLua_ProtectedCall( L, XGLua_GMClearCore, NULL, NULL, "gamemode-clear" );
		return fail;
	}

	g_szGMLuaError[0] = '\0';
	if( !XGLua_ProtectedCall( L, XGLua_GMFinalizeCore, NULL, XGLua_GMCaptureError, "gamemode-finalize" ) )
	{
		std::snprintf( err, errlen, "GAMEMODE install failed: %s",
			g_szGMLuaError[0] ? g_szGMLuaError : "unknown error" );
		XGLua_ProtectedCall( L, XGLua_GMClearCore, NULL, NULL, "gamemode-clear" );
		return XGLUA_GM_INTERNAL;
	}
	return XGLUA_GM_OK;
}

#endif // XGLUA_GAMEMODE_H
