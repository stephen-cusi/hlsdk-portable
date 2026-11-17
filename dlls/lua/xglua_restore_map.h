#ifndef XGLUA_RESTORE_MAP_H
#define XGLUA_RESTORE_MAP_H
#include <cstring>
#include <cctype>

static inline bool XGLua_RestoreNameEqual( const char *a, const char *b )
{
    if( !a || !b ) return false;
    while( *a && *b ) {
        unsigned char ac=(unsigned char)*a++, bc=(unsigned char)*b++;
        if( std::tolower(ac) != std::tolower(bc) ) return false;
    }
    return *a==*b;
}
static inline int XGLua_FindRestoredName( const char *name, const char *const *names, int count )
{
    if( !name || !*name ) return -1;
    for( int i=1; i<count; ++i )
        if( XGLua_RestoreNameEqual( name, names[i] ) ) return i;
    return -1;
}
static inline void XGLua_RemapSavedAmmo( int *amounts, int count,
    const char *const *oldNames, const char *const *newNames, int *scratch )
{
    for( int i=0; i<count; ++i ) scratch[i]=0;
    for( int i=1; i<count; ++i ) {
        int dst=XGLua_FindRestoredName( oldNames[i], newNames, count );
        if( dst>0 ) scratch[dst]=amounts[i];
    }
    for( int i=1; i<count; ++i ) amounts[i]=scratch[i];
}
static inline unsigned XGLua_RemapWeaponBits( unsigned bits, int count,
    const char *const *oldNames, const char *const *newNames )
{
    unsigned result=bits & 1u; // zero is never a weapon ID
    for( int i=1; i<count && i<32; ++i ) {
        if( !(bits & (1u<<i)) ) continue;
        if( !oldNames[i] || !*oldNames[i] ) { result |= (1u<<i); continue; }
        int dst=XGLua_FindRestoredName( oldNames[i], newNames, count );
        if( dst>0 && dst<32 ) result |= (1u<<dst);
    }
    return result;
}
#endif
