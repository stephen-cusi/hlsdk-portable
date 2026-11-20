#include <cstdio>
#include <string>
#include <vector>
#include "xglua_addon_weapons.h"

int main( int argc, char **argv )
{
    if( argc != 2 ) { std::puts( "expected fixture root" ); return 2; }
    std::vector<std::string> files;
    XGLua_FindAddonWeapons( argv[1], files );
    const char *expected[] = {
        "addons/alpha/lua/weapons/weapon_a.lua",
        "addons/zeta/lua/weapons/weapon_z.lua"
    };
    if( files.size() != 2 ) { std::printf( "unexpected count: %zu\n", files.size() ); return 1; }
    for( size_t i=0; i<files.size(); ++i ) {
        if( files[i] != expected[i] ) { std::printf( "unexpected file: %s\n", files[i].c_str() ); return 1; }
    }
    std::puts( "addon weapon discovery: ordering and filtering OK" );
    return 0;
}
