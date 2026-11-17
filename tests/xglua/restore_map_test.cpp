#include <cstdio>
#include <cstring>
#include "xglua_restore_map.h"
int main() {
    const char *oldAmmo[]={0,"9mm","grenade","rockets","missing"};
    const char *newAmmo[]={0,"rockets","9mm","added","GRENADE"};
    int ammo[]={0,60,12,4,8}, scratch[5];
    XGLua_RemapSavedAmmo(ammo,5,oldAmmo,newAmmo,scratch);
    if(ammo[1]!=4 || ammo[2]!=60 || ammo[3]!=0 || ammo[4]!=12) return 1;
    const char *oldW[]={0,"glock","alpha","beta",0};
    const char *newW[]={0,"BETA","glock","added",0};
    unsigned bits=(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4);
    unsigned got=XGLua_RemapWeaponBits(bits,5,oldW,newW);
    if(got!=((1u<<1)|(1u<<2)|(1u<<4))) return 2;
    std::puts("saved ammo and weapon names remap across registry changes OK");
    return 0;
}
