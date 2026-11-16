#ifndef XGLUA_SOUND_PRECACHE_H
#define XGLUA_SOUND_PRECACHE_H
#include <cstddef>
#include <cstring>
#include <cmath>

// This project builds with MAX_QPATH == 64 (including the terminating NUL).
#define XGLUA_SOUND_PATH_BYTES 64
#define XGLUA_SOUND_PER_WEAPON 32
#define XGLUA_SOUND_TOTAL 64

static inline bool XGLua_ValidSoundPath( const char *path, size_t size )
{
    if( !path || size < 5 || size >= XGLUA_SOUND_PATH_BYTES || path[0] == '/' || path[size - 1] == '/' ) return false;
    for( size_t i = 0; i < size; ++i )
    {
        unsigned char c = (unsigned char)path[i];
        if( !(( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
            ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.' || c == '/') ) return false;
        if( i && ((c == '.' && path[i-1] == '.') || (c == '/' && path[i-1] == '/')) ) return false;
    }
    // Only WAV samples are advertised by this server-side API; sentence and
    // streaming names are not filenames and follow different engine rules.
    return path[size-4] == '.' && (path[size-3] == 'w' || path[size-3] == 'W') &&
        (path[size-2] == 'a' || path[size-2] == 'A') && (path[size-1] == 'v' || path[size-1] == 'V');
}

// Read and validate the whole inherited or own declaration before touching
// the engine. Restore the stack on both success and failure.
static inline bool XGLua_ReadPrecacheSounds( lua_State *L, int swepIndex,
    char output[XGLUA_SOUND_PER_WEAPON][XGLUA_SOUND_PATH_BYTES], int *outputCount )
{
    int top = lua_gettop( L );
    if( swepIndex < 0 ) swepIndex = top + swepIndex + 1;
    *outputCount = 0;
    lua_getfield( L, swepIndex, "PrecacheSounds" );
    if( lua_isnil( L, -1 ) ) { lua_settop( L, top ); return true; }
    if( !lua_istable( L, -1 ) ) { lua_settop( L, top ); return false; }
    int table = lua_gettop( L ), count = 0, maximum = 0;
    lua_pushnil( L );
    while( lua_next( L, table ) != 0 )
    {
        if( lua_type( L, -2 ) != LUA_TNUMBER || lua_type( L, -1 ) != LUA_TSTRING )
        { lua_settop( L, top ); return false; }
        double number = lua_tonumber( L, -2 );
        if( !std::isfinite( number ) || number < 1 || number > XGLUA_SOUND_PER_WEAPON || std::floor( number ) != number )
        { lua_settop( L, top ); return false; }
        size_t length = 0;
        const char *path = lua_tolstring( L, -1, &length );
        if( !XGLua_ValidSoundPath( path, length ) ) { lua_settop( L, top ); return false; }
        int slot = (int)number - 1;
        std::memcpy( output[slot], path, length ); output[slot][length] = '\0';
        ++count;
        if( number > maximum ) maximum = (int)number;
        lua_pop( L, 1 );
    }
    lua_settop( L, top );
    if( maximum != count ) return false; // reject sparse or out-of-range arrays
    *outputCount = count;
    return true;
}

static inline bool XGLua_SoundNamesEqual( const char *a, const char *b )
{
    while( *a && *b )
    {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if( x >= 'A' && x <= 'Z' ) x = (unsigned char)( x + ('a' - 'A') );
        if( y >= 'A' && y <= 'Z' ) y = (unsigned char)( y + ('a' - 'A') );
        if( x != y ) return false;
    }
    return *a == *b;
}

template<class Exists>
static inline bool XGLua_ValidateSoundResources(
    char declared[XGLUA_SOUND_PER_WEAPON][XGLUA_SOUND_PATH_BYTES], int count,
    char registered[XGLUA_SOUND_TOTAL][XGLUA_SOUND_PATH_BYTES], int registeredCount, Exists exists )
{
    if( count < 0 || count > XGLUA_SOUND_PER_WEAPON || registeredCount < 0 || registeredCount > XGLUA_SOUND_TOTAL ) return false;
    int additional = 0;
    for( int i = 0; i < count; ++i )
    {
        if( !exists( declared[i] ) ) return false;
        bool found = false;
        for( int j = 0; j < registeredCount && !found; ++j ) found = XGLua_SoundNamesEqual( declared[i], registered[j] );
        for( int j = 0; j < i && !found; ++j ) found = XGLua_SoundNamesEqual( declared[i], declared[j] );
        if( !found ) ++additional;
    }
    return registeredCount + additional <= XGLUA_SOUND_TOTAL;
}
#endif
