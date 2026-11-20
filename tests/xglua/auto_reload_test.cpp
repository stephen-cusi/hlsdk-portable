#include <cassert>
#include <cstdio>
#include <xglua_auto_reload.h>
int main() {
 assert(XGLua_ShouldAutoReload(0,5,3,false,12,11));
 assert(!XGLua_ShouldAutoReload(1,5,3,false,12,11));
 assert(!XGLua_ShouldAutoReload(0,5,0,false,12,11));
 assert(!XGLua_ShouldAutoReload(0,-1,3,false,12,11));
 assert(!XGLua_ShouldAutoReload(0,5,3,true,12,11));
 assert(!XGLua_ShouldAutoReload(0,5,3,false,11,11));
 assert(!XGLua_ShouldAutoReload(0,5,3,false,10,11));
 puts("automatic reload: empty, reserve, flags, clip and firing deadline passed");
}
