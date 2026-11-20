#ifndef XGLUA_ADDON_WEAPONS_H
#define XGLUA_ADDON_WEAPONS_H

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// Filesystem-backed addons only. Each direct child under addons/ may supply
// lua/weapons/*.lua. Directory links are deliberately not followed.
inline bool XGLua_AddonComponent( const char *name )
{
    if( !name || !*name || std::strlen( name ) > 64 ) return false;
    for( const char *p = name; *p; ++p )
        if( !( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ) ) return false;
    return true;
}
inline bool XGLua_AddonLuaFile( const char *name )
{
    size_t n = name ? std::strlen( name ) : 0;
    if( n <= 4 || n > 128 || name[0] == '.' ) return false;
    for( size_t i=0; i<n-4; ++i )
        if( !( (name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
               (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-' ) ) return false;
    const char *e = name + n - 4;
    return e[0]=='.' && (e[1]=='l'||e[1]=='L') && (e[2]=='u'||e[2]=='U') && (e[3]=='a'||e[3]=='A');
}
inline bool XGLua_AddonRealDirectory( const char *path )
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA( path );
    return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_DIRECTORY ) && !( a & FILE_ATTRIBUTE_REPARSE_POINT );
#else
    struct stat st;
    return lstat( path, &st ) == 0 && S_ISDIR( st.st_mode );
#endif
}
inline void XGLua_AddonList( const char *dir, bool directories, std::vector<std::string> &out )
{
#ifdef _WIN32
    char mask[1024];
    int count = std::snprintf( mask, sizeof(mask), "%s/*", dir );
    if( count < 0 || (size_t)count >= sizeof(mask) ) return;
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA( mask, &data );
    if( h == INVALID_HANDLE_VALUE ) return;
    do {
        bool isdir = ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
        if( isdir != directories || ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) ) continue;
        if( directories ? XGLua_AddonComponent(data.cFileName) : XGLua_AddonLuaFile(data.cFileName) )
            out.push_back( data.cFileName );
    } while( FindNextFileA( h, &data ) );
    FindClose( h );
#else
    DIR *d = opendir( dir );
    if( !d ) return;
    struct dirent *entry;
    while( (entry = readdir(d)) != NULL ) {
        if( directories ? !XGLua_AddonComponent(entry->d_name) : !XGLua_AddonLuaFile(entry->d_name) ) continue;
        char path[1024]; int count = std::snprintf( path, sizeof(path), "%s/%s", dir, entry->d_name );
        if( count < 0 || (size_t)count >= sizeof(path) ) continue;
        struct stat st;
        if( lstat(path, &st) != 0 ) continue;
        if( directories ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode) ) out.push_back(entry->d_name);
    }
    closedir(d);
#endif
    std::sort( out.begin(), out.end() );
}
inline void XGLua_FindAddonScripts( const char *root, const char *kind, std::vector<std::string> &result )
{
    char path[1024];
    int n = std::snprintf(path, sizeof(path), "%s/addons", root ? root : "");
    if( !root || !*root || n < 0 || (size_t)n >= sizeof(path) || !XGLua_AddonRealDirectory(path) ) return;
    std::vector<std::string> addons;
    XGLua_AddonList(path, true, addons);
    for( size_t i=0; i<addons.size(); ++i ) {
        n = std::snprintf(path, sizeof(path), "%s/addons/%s/lua", root, addons[i].c_str());
        if( n < 0 || (size_t)n >= sizeof(path) || !XGLua_AddonRealDirectory(path) ) continue;
        n = std::snprintf(path, sizeof(path), "%s/addons/%s/lua/%s", root, addons[i].c_str(), kind);
        if( n < 0 || (size_t)n >= sizeof(path) || !XGLua_AddonRealDirectory(path) ) continue;
        std::vector<std::string> files;
        XGLua_AddonList(path, false, files);
        for( size_t j=0; j<files.size(); ++j ) {
            std::string rel = "addons/" + addons[i] + "/lua/" + kind + "/" + files[j];
            if( rel.size() < 256 ) result.push_back(rel);
        }
    }
}
inline void XGLua_FindAddonWeapons( const char *root, std::vector<std::string> &result )
{ XGLua_FindAddonScripts( root, "weapons", result ); }
inline void XGLua_FindAddonEntities( const char *root, std::vector<std::string> &result )
{ XGLua_FindAddonScripts( root, "entities", result ); }
#endif
