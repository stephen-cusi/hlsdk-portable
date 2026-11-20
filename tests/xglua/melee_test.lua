SERVER=true; IN_ATTACK2=2048; file={Exists=function()return true end}
local single=true; game={SinglePlayer=function() return single end}
function includeAddon(n,p) return assert(loadfile('../../addons/'..n..'/lua/'..p))() end
function CurTime() return now end
function IsValid(x) return x~=nil end
function Angle(x,y,z) return {x=x,y=y,z=z} end
local saved={}; weapons={Register=function(w,n) saved[n]=w end}
SWEP={}; assert(loadfile('../../addons/opfor_knife/lua/weapons/weapon_knife.lua'))()
SWEP={}; assert(loadfile('../../addons/opfor_pipewrench/lua/weapons/weapon_pipewrench.lua'))()
local held=false; local owner={KeyDown=function(_,key) assert(key==IN_ATTACK2); return held end,ViewPunch=function() end}
local function mock(def)
 local w=setmetatable({}, {__index=def})
 function w:TraceMeleeAttack(range,damage) assert(range==32); self.damage=damage; self.hits=(self.hits or 0)+1; return {Hit=self.hit or false,HitFlesh=self.flesh or false} end
 function w:SendOwnerAttackAnimation() end; function w:SendWeaponAnim(seq) self.anim=seq end
 function w:EmitSound(s) self.sound=s end; function w:SetNextAttackDelay(n) self.delay=n end
 function w:SetNextSecondaryFire(n) self.secondary=n end; function w:SetNextPrimaryFire(n) self.primary=n end
 function w:SetWeaponIdleTime(n) self.idle=n end; function w:GetWeaponIdleTime() return self.idle or 0 end
 function w:SetNextAttack(n) self.lock=n end; function w:GetOwner() return owner end
 return w
end
now=10; local knife=mock(assert(saved.weapon_knife)); knife:PrimaryAttack()
assert(knife.damage==10 and knife.delay==0.5 and knife.hits==1)
knife.hit=true; knife:PrimaryAttack(); assert(knife.damage==5 and knife.delay==0.25 and knife.anim==6)
single=false; knife:PrimaryAttack(); assert(knife.damage==10); single=true
local wrench=mock(assert(saved.weapon_pipewrench)); wrench.hit=true; wrench.flesh=true
wrench:PrimaryAttack(); assert(wrench.damage==20 and wrench.delay==0.5 and wrench.anim==7)
held=true; wrench:SecondaryAttack(); assert(wrench.anim==11)
now=11.5; wrench:Think(); assert(wrench.hits==1)
held=false; wrench:Think(); assert(wrench.hits==2 and wrench.damage==55 and wrench.anim==12)
assert(wrench.primary==12.6 and wrench.secondary==12.6)
now=20; held=true; wrench:SecondaryAttack(); held=false; now=20.2; wrench:Think(); assert(wrench.hits==2)
now=21; wrench:Think(); assert(wrench.hits==3 and wrench.damage==45)
now=30; held=true; wrench:SecondaryAttack(); wrench:Holster(); held=false; now=33; wrench:Think(); assert(wrench.hits==3)
print('Knife and wrench: hits, miss, charge hold/release and holster passed')
