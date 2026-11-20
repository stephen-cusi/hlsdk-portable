-- Opposing Force M40A1, server-authoritative Lua SWEP.
-- Original content is supplied by the player's own Opposing Force installation.
local cfg = includeAddon("opfor_m40a1", "config.lua")
local assets = cfg.Required
for _, path in ipairs(assets) do
 if not file.Exists(path) then print("[M40A1] missing asset: " .. path); return end
end
game.AddAmmoType({name=cfg.Weapon.Primary.Ammo, maxcarry=cfg.Weapon.Primary.MaxAmmo})

for key, value in pairs(cfg.Weapon) do SWEP[key] = value end
SWEP.PrecacheSounds = cfg.Sounds.Precache

function SWEP:Initialize() self._emptyReload=false end
function SWEP:SecondaryAttack()
 local p=self:GetOwner(); if not IsValid(p) then return end
 p:SetFOV(p:GetFOV()~=0 and 0 or cfg.ZoomFOV)
 self:EmitSound(cfg.Sounds.Zoom); self:SetNextSecondaryFire(CurTime()+0.5)
end
function SWEP:Holster()
 self._emptyReload=false
 local p=self:GetOwner()
 if IsValid(p) and p:GetFOV()~=0 then p:SetFOV(0); self:EmitSound(cfg.Sounds.Zoom) end
 self:SendWeaponAnim(8); self:SetNextAttack(1.0)
end
function SWEP:PrimaryAttack()
 if self._emptyReload then return end
 local p=self:GetOwner(); if not IsValid(p) then return end
 if self:Clip1()<=0 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.2); return end
 if p:WaterLevel()==3 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.15); return end
 self:TakePrimaryAmmo(1); self:SendWeaponAnim(self:Clip1()==0 and 3 or 2)
 self:MuzzleFlash(); self:SendOwnerAttackAnimation(); self:EmitSound(cfg.Sounds.Fire)
 -- 100 is the installed skillopfor.cfg value. Explicit bullet type avoids
 -- GoldSrc's ALWAYSGIB branch for custom damage greater than 16.
 self:ShootBullet(cfg.Damage,1,Vector(cfg.Spread,cfg.Spread,0),DMG_BULLET)
 p:ViewPunch(Angle(-5,0,0)); self:SetNextPrimaryFire(CurTime()+cfg.FireDelay)
 self:SetWeaponIdleTime(CurTime()+68/38)
end
function SWEP:Reload()
 if self:Clip1()>=cfg.Weapon.Primary.ClipSize or self:Ammo1()<=0 or self:IsReloading() or self._emptyReload then return end
 local p=self:GetOwner(); if not IsValid(p) then return end
 if p:GetFOV()~=0 then self:SecondaryAttack() end
 if self:Clip1()==0 then
  if self:DefaultReload(cfg.Weapon.Primary.ClipSize,4,80/34) then self._emptyReload=true; self:SetNextPrimaryFire(CurTime()+2.25); self:SetWeaponIdleTime(CurTime()+2.25) end
 else self:DefaultReload(cfg.Weapon.Primary.ClipSize,6,2.25) end
end
function SWEP:WeaponIdle()
 if CurTime()<self:GetWeaponIdleTime() then return end
 if self._emptyReload then
  self._emptyReload=false; self:SendWeaponAnim(5)
  self:SetNextPrimaryFire(CurTime()+49/27); self:SetWeaponIdleTime(CurTime()+49/27)
 elseif self:Clip1()<=0 then self:SendWeaponAnim(7); self:SetWeaponIdleTime(CurTime()+5)
 else self:SendWeaponAnim(1); self:SetWeaponIdleTime(CurTime()+67.5/16) end
end
weapons.Register(SWEP,"weapon_sniperrifle")
