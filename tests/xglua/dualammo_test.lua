-- Run from the repository root with LuaJIT. Engine methods are test doubles.
local script = (arg[0] or ""):gsub("\\", "/")
local root = assert(script:match("^(.-)tests/xglua/[^/]+$"), "run from repository root")
if root == "" then root = "./" end

dofile(root .. "lua/includes/modules/weapons.lua")
SERVER = true
SWEP = {}
dofile(root .. "lua/weapons/weapon_xgl_dualammo.lua")
local def = assert(weapons.GetStored("weapon_xgl_dualammo"))
assert(def.Secondary.ClipSize == -1 and def.Secondary.DefaultAmmo == 4 and def.Secondary.MaxAmmo == 10)
assert(def.PrecacheSounds[1] == "weapons/hks1.wav" and def.PrecacheSounds[2] == "weapons/glauncher.wav")
local clock = 100
CurTime = function() return clock end
IsValid = function(v) return v ~= nil end
local vecmt = {}
vecmt.__mul = function(a, b) return setmetatable({x=a.x*b, y=a.y*b, z=a.z*b}, vecmt) end
vecmt.__add = function(a, b) return setmetatable({x=a.x+b.x, y=a.y+b.y, z=a.z+b.z}, vecmt) end
Vector = function(x,y,z) return setmetatable({x=x,y=y,z=z}, vecmt) end
Angle = function(p,y,r) return {p=p,y=y,r=r} end
local owner = {
    reserve = 4,
    IsPlayer = function() return true end,
    Alive = function() return true end,
    GetAmmoCount = function(self, ammo) assert(ammo == "xgl_dual_grenade"); return self.reserve end,
    GetAimVector = function() return Vector(1,0,0) end,
    EyePos = function() return Vector(1,2,3) end,
    ViewPunch = function(self, punch) self.punch=punch end
}
local create = true
util = { ThrowGrenade = function(ply, pos, vel, contact)
    assert(ply == owner and pos.x == 17 and vel.x == 800 and contact == true)
    if create then return {} end
end }
local w = {
    clip = 30,
    GetOwner = function(self) return self.owner end,
    Clip1 = function(self) return self.clip end,
    TakePrimaryAmmo = function(self,n) self.clip=self.clip-n end,
    TakeSecondaryAmmo = function(self,n) self.owner.reserve=self.owner.reserve-n end,
    ShootBullet = function(self,damage,count,spread) self.bullet={damage,count,spread} end,
    SendWeaponAnim = function(self,n) self.anim=n end,
    MuzzleFlash = function(self) self.flash=(self.flash or 0)+1 end,
    SendOwnerAttackAnimation = function(self) self.ownerAnim=(self.ownerAnim or 0)+1 end,
    EmitSound = function(self, sound) self.sound=sound end,
    SetNextPrimaryFire = function(self,t) self.nextPrimary=t end,
    SetNextSecondaryFire = function(self,t) self.nextSecondary=t end,
    SetWeaponIdleTime = function(self,t) self.idle=t end,
    GetWeaponIdleTime = function(self) return self.idle end,
    PlayEmptySound = function(self) self.empty=(self.empty or 0)+1 end,
    DefaultReload = function(self,clip,anim,delay) self.reload={clip,anim,delay}; return true end
}
setmetatable(w, {__index=def})
w.owner=owner
w:Deploy()
assert(w.nextPrimary==100.5 and w.nextSecondary==100.5 and w.idle==103)
w:PrimaryAttack()
assert(w.clip==29 and w.bullet[1]==8 and w.bullet[2]==1 and w.flash==1 and w.ownerAnim==1)
assert(w.sound == def.PrecacheSounds[1])
assert(w.nextPrimary==100.12 and w.nextSecondary==100.12 and w.anim==5)
w:SecondaryAttack()
assert(owner.reserve==3 and w.clip==29 and w.anim==2 and w.ownerAnim==2)
assert(w.sound == def.PrecacheSounds[2])
assert(w.nextPrimary==101 and w.nextSecondary==101)
create=false
w:SecondaryAttack()
assert(owner.reserve==3, "failed grenade spawn must not consume ammo")
owner.reserve=0
assert(not w:CanSecondaryAttack() and w.empty==1 and w.nextSecondary==100.2)
w.Secondary={Ammo="none"}
assert(w:CanSecondaryAttack() and w:Ammo2()==0)
w.Secondary=def.Secondary
assert(w:Reload() and w.reload[1]==30 and w.reload[2]==3 and w.reload[3]==1.5)
w.idle=99
w:WeaponIdle()
assert(w.anim==0 and w.idle==110)
w.idle=101
w.anim=7
w:WeaponIdle()
assert(w.anim==7 and w.idle==101)
-- Secondary may intentionally share the primary ammunition name.
w.Primary={Ammo="shared", ClipSize=30}
w.Secondary={Ammo="shared"}
owner.GetAmmoCount=function(self, ammo) assert(ammo=="shared"); return self.reserve end
owner.reserve=2
assert(w:Ammo2()==2 and w:CanSecondaryAttack())
owner.reserve=0
clock=101
assert(not w:CanSecondaryAttack() and w.nextSecondary==101.2)
clock=102
assert(not w:CanSecondaryAttack() and w.nextSecondary==102.2)
w.owner=nil
assert(w:Ammo2()==0 and not w:CanSecondaryAttack())
w:PrimaryAttack()
assert(w.clip==29)
-- Ammo=none does not require a reserve, matching the native no-ammo mode.
w.Secondary={Ammo="none"}
assert(w:Ammo2()==0 and w:CanSecondaryAttack())
print("dual ammunition weapon Lua tests passed")
