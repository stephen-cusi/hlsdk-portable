function includeAddon(name,path) return assert(loadfile("../../addons/"..name.."/lua/"..path))() end
SERVER=true; SWEP={}; ENT={}; DMG_BULLET=2
file={Exists=function() return true end}; game={AddAmmoType=function(t) assert(t.name=='762' and t.maxcarry==15) end}
weapons={Register=function(w,n) assert(n=='weapon_sniperrifle') end}
function Vector(x,y,z) return {x=x,y=y,z=z} end; Angle=Vector
function IsValid(x) return x~=nil end
local now=10; function CurTime() return now end
assert(loadfile('../../addons/opfor_m40a1/lua/weapons/weapon_sniperrifle.lua'))()
assert(loadfile('../../addons/opfor_m40a1/lua/entities/xgl_ammo_762.lua'))()
local fov=0; local water=0; local clip=5; local reserve=10
local p={GetFOV=function() return fov end,SetFOV=function(_,x) fov=x end,WaterLevel=function() return water end,ViewPunch=function() end}
local w=setmetatable({}, {__index=SWEP})
function w:GetOwner() return p end; function w:SetNextSecondaryFire(x) self.secondary=x end
function w:EmitSound() end; function w:Clip1() return clip end; function w:Ammo1() return reserve end
function w:IsReloading() return false end; function w:TakePrimaryAmmo(n) clip=clip-n end
function w:SendWeaponAnim(x) self.anim=x end; function w:MuzzleFlash() end; function w:SendOwnerAttackAnimation() end
function w:ShootBullet(d,n,s,flags) assert(d==100 and n==1 and s.x==0.001 and flags==DMG_BULLET) end
function w:SetNextPrimaryFire(x) self.primary=x end; function w:SetWeaponIdleTime(x) self.idle=x end
function w:GetWeaponIdleTime() return self.idle or 0 end; function w:SetNextAttack(x) self.lock=x end
function w:PlayEmptySound() self.empty=(self.empty or 0)+1 end
function w:DefaultReload(n,a,t) assert(n==5); self.anim=a; self.duration=t; return true end
w:SecondaryAttack(); assert(fov==15 and w.secondary==10.5)
w:PrimaryAttack(); assert(clip==4 and w.anim==2 and w.primary==11.75)
w:Reload(); assert(fov==0 and w.anim==6 and w.duration==2.25)
clip=1; w:PrimaryAttack(); assert(clip==0 and w.anim==3)
w:Reload(); assert(w.anim==4 and w._emptyReload and math.abs(w.duration-80/34)<0.0001)
now=13; clip=5; w:WeaponIdle(); assert(not w._emptyReload and w.anim==5)
water=3; clip=1; w:PrimaryAttack(); assert(w.empty==1 and clip==1)
fov=15; w:Holster(); assert(fov==0 and w.anim==8 and w.lock==1)
local count,removed=10,0
local other={IsPlayer=function() return true end,GetAmmoCount=function() return count end,GiveAmmo=function(_,n,name,max) assert(n==5 and name=='762' and max==15); count=math.min(count+n,max) end}
local box={EmitSound=function() end,Remove=function() removed=removed+1 end}
ENT.Touch(box,other); assert(count==15 and removed==1)
ENT.Touch(box,other); assert(count==15 and removed==1)
print('M40A1: zoom, fire, both reloads, water, holster and ammo passed')
