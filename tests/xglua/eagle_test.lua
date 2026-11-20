function includeAddon(name,path) return assert(loadfile("../../addons/"..name.."/lua/"..path))() end
SERVER=true
DMG_BULLET=2
SWEP={}
file={Exists=function() return true end}
local entdef
scripted_ents={Register=function(t,n) assert(n=='xgl_eagle_laser'); entdef=t end}
weapons={Register=function(t,n) assert(n=='weapon_eagle') end}
local vmeta={__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end,__mul=function(a,n) return Vector(a.x*n,a.y*n,a.z*n) end}
function Vector(x,y,z) return setmetatable({x=x,y=y,z=z},vmeta) end
Angle=Vector
local now=10
function CurTime() return now end
function IsValid(v) return v~=nil and not v.removed end
local spot
ents={Create=function()
 spot=setmetatable({SetPos=function() end,SetOwner=function() end,SetAimFollower=function() end,Spawn=function() end,Remove=function(s) s.removed=true end},{__index=entdef})
 return spot
end}
assert(loadfile('../../addons/opfor_eagle/lua/weapons/weapon_eagle.lua'))()
ENT={}
assert(loadfile('../../addons/opfor_eagle/lua/entities/xgl_eagle_laser.lua'))()
entdef=ENT
local water,clip,reserve=0,7,20
local p={Alive=function() return true end, WaterLevel=function() return water end, EyePos=function() return Vector(0,0,0) end,
 GetAimVector=function() return Vector(1,0,0) end, ViewPunch=function() end}
local w=setmetatable({}, {__index=SWEP})
function w:GetOwner() return p end
function w:Clip1() return clip end
function w:TakePrimaryAmmo(n) clip=clip-n end
function w:SendWeaponAnim(n) self.anim=n end
function w:MuzzleFlash() end
function w:SendOwnerAttackAnimation() end
function w:EmitSound(s) self.sound=s end
function w:ShootBullet(d,n,s,flags) assert(flags==DMG_BULLET); self.damage=d; self.spread=s.x end
function w:SetNextPrimaryFire(t) self.primary=t end
function w:SetNextSecondaryFire(t) self.secondary=t end
function w:SetWeaponIdleTime(t) self.idle=t end
function w:PlayEmptySound() self.empty=(self.empty or 0)+1 end
function w:Ammo1() return reserve end
function w:IsReloading() return false end
function w:DefaultReload(n,anim,delay) self.reloadanim=anim; self.reloaddelay=delay; return true end
w:PrimaryAttack(); assert(clip==6 and w.spread==0.1 and w.damage==34 and w.primary==10.22)
w:SecondaryAttack(); assert(w._laserOn and IsValid(spot) and w.secondary==10.5)
w:PrimaryAttack(); assert(clip==5 and w.spread==0.001 and w.primary==10.5 and w._laserUntil==10.6)
w:Reload(); assert(w.reloadanim==8 and w.reloaddelay==1.5 and w._laserUntil==11.6)
clip=0; w:Reload(); assert(w.reloadanim==7)
w:PrimaryAttack(); assert(w.empty==1 and clip==0)
clip=1; water=3; w:PrimaryAttack(); assert(w.empty==2 and clip==1)
w:Holster(); assert(spot.removed and w._spot==nil)
print('Eagle fire modes, water, empty clip, reload and laser lifecycle passed')
