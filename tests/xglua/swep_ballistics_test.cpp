#include <cassert>
#include <limits>
#include <cstdio>
using ULONG = unsigned long;
struct Vector { float x,y,z; Vector(float a=0,float b=0,float c=0):x(a),y(b),z(c) {} Vector operator+(const Vector& r) const { return Vector(x+r.x,y+r.y,z+r.z); } };
struct Entvars { Vector v_angle, punchangle; };
struct Globals { Vector v_forward, v_right, v_up; } globals, *gpGlobals = &globals;
static Vector basisAngles;
void UTIL_MakeVectors( Vector v ) { basisAngles=v; gpGlobals->v_forward=Vector(0,1,0); gpGlobals->v_right=Vector(1,0,0); gpGlobals->v_up=Vector(0,0,1); }
static const int BULLET_PLAYER_9MM=2;
#include "xglua_ballistics.h"
struct FakePlayer {
 Entvars *pev; int random_seed=123; bool fired=false; ULONG shots=0; Vector start,dir,spread; float distance=0; int type=0,tracer=-1,damage=0,seed=0; Entvars *attacker=nullptr;
 Vector GetGunPosition() { return Vector(3,4,5); }
 void FireBulletsPlayer(ULONG n, Vector s, Vector d, Vector sp, float dist, int t, int tr, int dmg, Entvars *a, int random) {
  fired=true; shots=n; start=s; dir=d; spread=sp; distance=dist; type=t; tracer=tr; damage=dmg; attacker=a; seed=random;
  assert(gpGlobals->v_right.x==1 && gpGlobals->v_up.z==1);
 }
};
int main() {
 assert(XGLua_ValidBulletDamage(1) && XGLua_ValidBulletDamage(1000));
 assert(!XGLua_ValidBulletDamage(0) && !XGLua_ValidBulletDamage(1.5) && !XGLua_ValidBulletDamage(std::numeric_limits<double>::infinity()));
 assert(XGLua_ValidBulletCount(1) && XGLua_ValidBulletCount(32));
 assert(!XGLua_ValidBulletCount(33) && !XGLua_ValidBulletCount(-1) && !XGLua_ValidBulletCount(std::numeric_limits<double>::quiet_NaN()));
 assert(XGLua_ValidBulletSpread(Vector(0.1f,0.2f,0)) && !XGLua_ValidBulletSpread(Vector(1.1f,0,0)));
 Entvars ent; ent.v_angle=Vector(2,3,4); ent.punchangle=Vector(-1,2,0);
 FakePlayer player; player.pev=&ent;
 XGLua_FirePlayerBullets(&player, 7, 5, Vector(.1f,.2f,0));
 assert(player.fired && player.shots==5 && player.damage==7 && player.attacker==&ent && player.seed==123);
 assert(basisAngles.x==1 && basisAngles.y==5 && basisAngles.z==4);
 assert(player.start.x==3 && player.start.y==4 && player.start.z==5 && player.dir.y==1);
 assert(player.spread.x==.1f && player.spread.y==.2f && player.distance==8192 && player.type==BULLET_PLAYER_9MM && player.tracer==0);
 std::puts("SWEP hitscan validation, player ownership and aim basis OK");
}
