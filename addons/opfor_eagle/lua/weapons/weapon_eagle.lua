-- Opposing Force Desert Eagle; assets supplied by the player's installation.
local cfg = includeAddon("opfor_eagle", "config.lua")
local required = cfg.Required
for _, path in ipairs(required) do
 if not file.Exists(path) then print("[Eagle] missing asset: " .. path); return end
end

for key, value in pairs(cfg.Weapon) do SWEP[key] = value end
SWEP.PrecacheSounds = cfg.Sounds.Precache

function SWEP:EnsureSpot()
 if not self._laserOn or IsValid(self._spot) then return end
 local p=self:GetOwner(); if not IsValid(p) then return end
 local ok, e=pcall(ents.Create, cfg.Laser.Class)
 if not ok or not IsValid(e) then print("[Eagle] laser creation failed: " .. tostring(e)); return end
 e.Weapon=self; e.OwnerPlayer=p; e:SetOwner(p); e:SetAimFollower(true); e:SetPos(p:EyePos()); e:Spawn(); self._spot=e
end
function SWEP:RemoveSpot()
 if IsValid(self._spot) then self._spot:Remove() end
 self._spot=nil
end
function SWEP:Deploy() self:EnsureSpot(); self:SetWeaponIdleTime(CurTime()+1) end
function SWEP:Holster() self:RemoveSpot() end
function SWEP:SecondaryAttack()
 self._laserOn=not self._laserOn; self:SetNextSecondaryFire(CurTime()+0.5)
 if self._laserOn then self:EnsureSpot(); self:EmitSound(cfg.Sounds.On)
 else self:RemoveSpot(); self:EmitSound(cfg.Sounds.Off) end
end
function SWEP:PrimaryAttack()
 local p=self:GetOwner(); if not IsValid(p) then return end
 if self:Clip1() <= 0 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.2); return end
 if p:WaterLevel()==3 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.15); return end
 self:TakePrimaryAmmo(1); self:SendWeaponAnim(self:Clip1()==0 and 6 or 5)
 self:MuzzleFlash(); self:SendOwnerAttackAnimation(); self:EmitSound(cfg.Sounds.Fire)
 local spread=self._laserOn and cfg.SpreadLaser or cfg.SpreadHip
 self:ShootBullet(cfg.Damage, 1, Vector(spread,spread,0), DMG_BULLET)
 p:ViewPunch(Angle(-4,0,0))
 if self._laserOn then self._laserUntil=CurTime()+0.6 end
 self:SetNextPrimaryFire(CurTime() + (self._laserOn and cfg.DelayLaser or cfg.DelayHip))
 self:SetWeaponIdleTime(CurTime()+math.random(10,15))
end
function SWEP:Reload()
 if self:Clip1()>=cfg.Weapon.Primary.ClipSize or self:Ammo1()<=0 or self:IsReloading() then return end
 local ok=self:DefaultReload(cfg.Weapon.Primary.ClipSize, self:Clip1()==0 and 7 or 8, 1.5)
 if ok and self._laserOn then self._laserUntil=CurTime()+1.6; self:SetNextSecondaryFire(CurTime()+1.5) end
end
function SWEP:WeaponIdle()
 self:EnsureSpot()
 if self:Clip1()==0 or CurTime()<self:GetWeaponIdleTime() then return end
 local seq, delay
 if self._laserOn then if math.random()<0.5 then seq,delay=3,2.5 else seq,delay=4,2 end
 else local x=math.random(); if x<=0.3 then seq,delay=0,2.5 elseif x<=0.6 then seq,delay=1,2.5 else seq,delay=2,1.63 end end
 self:SendWeaponAnim(seq); self:SetWeaponIdleTime(CurTime()+delay)
end
weapons.Register(SWEP,"weapon_eagle")
