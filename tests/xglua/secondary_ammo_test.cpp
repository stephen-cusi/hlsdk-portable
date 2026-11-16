#include <cassert>
#include <cstdio>
#include "xglua_secondary_ammo.h"
int main() {
  assert(XGLua_SecondaryPickupAllowed(true,true,true,4,3,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(false,true,true,4,3,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,false,true,4,3,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,true,false,4,3,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,true,true,0,3,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,true,true,4,0,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,true,true,4,32,32,0,10));
  assert(!XGLua_SecondaryPickupAllowed(true,true,true,4,3,32,10,10));
  int p=0,s=0;
  int a=XGLua_DuplicateAmmo(true,true,[&](){++p;return 1;},[&](){++s;return 1;});
  assert(a==1 && p==1 && s==0);
  p=s=0;
  a=XGLua_DuplicateAmmo(false,true,[&](){++p;return -1;},[&](){++s;return 1;});
  assert(a==1 && p==1 && s==1);
  p=s=0;
  a=XGLua_DuplicateAmmo(false,false,[&](){++p;return 1;},[&](){++s;return 0;});
  assert(a==0 && p==0 && s==1);
  assert(XGLua_SecondaryRemaining(4,1)==3 && XGLua_SecondaryRemaining(4,9)==0);
  assert(XGLua_SecondaryRemaining(4,0)==4 && XGLua_SecondaryRemaining(4,-1)==4);
  assert(XGLua_WeaponIdleDeadline(100.f,1.f)==101.f && XGLua_WeaponIdleDeadline(100.f,3.f)==103.f);
  std::puts("secondary ammo pickup, duplicate routing, consumption and idle deadlines OK");
}
