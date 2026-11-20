-- Opposing Force Desert Eagle; assets supplied by the player's installation.
local required = { "models/v_desert_eagle.mdl", "models/w_desert_eagle.mdl",
 "models/p_desert_eagle.mdl", "sprites/xgeagle_dot.spr",
 "sound/weapons/desert_eagle_fire.wav", "sound/weapons/desert_eagle_reload.wav",
 "sound/weapons/desert_eagle_sight.wav", "sound/weapons/desert_eagle_sight2.wav" }
for _, path in ipairs(required) do
 if not file.Exists(path) then print("[Eagle] missing asset: " .. path); return end
end

local spot = { Type="anim", Base="base_entity", Model="sprites/xgeagle_dot.spr" }
function spot:Initialize()
 self:SetSolid(SOLID_NOT); self:SetMoveType(MOVETYPE_NONE)
 self:SetRenderMode(3); self:SetRenderFX(14); self:SetRenderAmount(255); self:SetModelScale(0.5)
end
function spot:Think()
 local w, p = self.Weapon, self.OwnerPlayer
 if not IsValid(w) or not IsValid(p) or not p:Alive() or not w._laserOn then self:Remove(); return end
 local hidden = CurTime() < (w._laserUntil or 0)
 self:SetNoDraw(hidden)
 if not hidden then
  local start, aim = p:EyePos(), p:GetAimVector()
  local tr = util.TraceLine({start=start, endpos=start + aim * 8192, filter=p})
  self:SetPos(tr.HitPos)
 end
 self:NextThink(CurTime() + 0.001)
end
scripted_ents.Register(spot, "xgl_eagle_laser")

SWEP.PrintName="Desert Eagle"; SWEP.ViewModel="models/v_desert_eagle.mdl"
SWEP.WorldModel="models/w_desert_eagle.mdl"; SWEP.PlayerModel="models/p_desert_eagle.mdl"
SWEP.DeploySequence=9; SWEP.AnimExtension="onehanded"; SWEP.Slot=1; SWEP.SlotPos=2; SWEP.Weight=15
SWEP.Primary={ClipSize=7, DefaultClip=7, MaxAmmo=36, Ammo="357", Automatic=true}
SWEP.Secondary={ClipSize=-1, Ammo="none", Automatic=false}
SWEP.PrecacheSounds={"weapons/desert_eagle_fire.wav", "weapons/desert_eagle_reload.wav",
 "weapons/desert_eagle_sight.wav", "weapons/desert_eagle_sight2.wav"}

function SWEP:EnsureSpot()
 if not self._laserOn or IsValid(self._spot) then return end
 local p=self:GetOwner(); if not IsValid(p) then return end
 local ok, e=pcall(ents.Create, "xgl_eagle_laser")
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
 if self._laserOn then self:EnsureSpot(); self:EmitSound("weapons/desert_eagle_sight.wav")
 else self:RemoveSpot(); self:EmitSound("weapons/desert_eagle_sight2.wav") end
end
function SWEP:PrimaryAttack()
 local p=self:GetOwner(); if not IsValid(p) then return end
 if self:Clip1() <= 0 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.2); return end
 if p:WaterLevel()==3 then self:PlayEmptySound(); self:SetNextPrimaryFire(CurTime()+0.15); return end
 self:TakePrimaryAmmo(1); self:SendWeaponAnim(self:Clip1()==0 and 6 or 5)
 self:MuzzleFlash(); self:SendOwnerAttackAnimation(); self:EmitSound("weapons/desert_eagle_fire.wav")
 local spread=self._laserOn and 0.001 or 0.1
 self:ShootBullet(34, 1, Vector(spread,spread,0), DMG_BULLET)
 p:ViewPunch(Angle(-4,0,0))
 if self._laserOn then self._laserUntil=CurTime()+0.6 end
 self:SetNextPrimaryFire(CurTime() + (self._laserOn and 0.5 or 0.22))
 self:SetWeaponIdleTime(CurTime()+math.random(10,15))
end
function SWEP:Reload()
 if self:Clip1()>=7 or self:Ammo1()<=0 or self:IsReloading() then return end
 local ok=self:DefaultReload(7, self:Clip1()==0 and 7 or 8, 1.5)
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
