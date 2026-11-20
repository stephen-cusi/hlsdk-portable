local cfg=includeAddon("opfor_pipewrench","config.lua")
for _,model in ipairs({cfg.Weapon.ViewModel,cfg.Weapon.WorldModel,cfg.Weapon.PlayerModel}) do
 if not file.Exists(model) then print("[PipeWrench] missing "..model); return end
end
for k,v in pairs(cfg.Weapon) do SWEP[k]=v end
SWEP.PrecacheSounds={}
for _,group in pairs(cfg.Sounds) do
 if type(group)=="table" then for _,sound in ipairs(group) do SWEP.PrecacheSounds[#SWEP.PrecacheSounds+1]=sound end
 else SWEP.PrecacheSounds[#SWEP.PrecacheSounds+1]=group end
end
local function sound(self,group) self:EmitSound(group[math.random(1,#group)]) end
function SWEP:Deploy() self._charge=nil end
function SWEP:Holster() self._charge=nil; self:SendWeaponAnim(4); self:SetNextAttack(0.5) end
function SWEP:PrimaryAttack()
 if self._charge then return end
 local now=CurTime()
 local damage=cfg.Damage
 if game.SinglePlayer() and self._lastSwing and now-self._lastSwing<1 then damage=damage/2 end
 self._lastSwing=now
 local result=self:TraceMeleeAttack(cfg.Range,damage)
 self:SendOwnerAttackAnimation()
 if result.Hit then
  self._swing=1-(self._swing or 0); self:SendWeaponAnim(self._swing==1 and 7 or 9)
  sound(self,result.HitFlesh and cfg.Sounds.Body or cfg.Sounds.Wall)
  self:SetNextAttackDelay(0.5); self:SetNextSecondaryFire(CurTime()+0.5)
 else
  self:SendWeaponAnim(math.random(0,1)==0 and 6 or 8); sound(self,cfg.Sounds.Miss)
  self:SetNextAttackDelay(0.7); self:SetNextSecondaryFire(CurTime()+0.7)
 end
 self:SetWeaponIdleTime(CurTime()+5)
end
function SWEP:SecondaryAttack()
 if not self._charge then self._charge=CurTime(); self:SendWeaponAnim(11) end
 self:SetNextSecondaryFire(CurTime()+0.1)
end
function SWEP:Think()
 if not self._charge then return end
 local owner=self:GetOwner()
 if not IsValid(owner) then self._charge=nil; return end
 if owner:KeyDown(IN_ATTACK2) or CurTime()<self._charge+cfg.ChargeMin then return end
 local damage=math.min(cfg.ChargeMax,(CurTime()-self._charge)*cfg.Damage+cfg.ChargeBase)
 self._charge=nil
 local result=self:TraceMeleeAttack(cfg.Range,damage)
 self:SendOwnerAttackAnimation(); owner:ViewPunch(Angle(-2,0,0))
 if result.Hit then self:SendWeaponAnim(12); sound(self,result.HitFlesh and cfg.Sounds.HeavyBody or cfg.Sounds.Wall)
 else self:SendWeaponAnim(13); self:EmitSound(cfg.Sounds.HeavyMiss) end
 self:SetNextPrimaryFire(CurTime()+cfg.HeavyDelay); self:SetNextSecondaryFire(CurTime()+cfg.HeavyDelay)
 self:SetWeaponIdleTime(CurTime()+cfg.HeavyDelay)
end
function SWEP:WeaponIdle()
 if self._charge or CurTime()<self:GetWeaponIdleTime() then return end
 local f=math.random(); local seq=f<=0.3 and 0 or (f<=0.6 and 1 or 2)
 self:SendWeaponAnim(seq); self:SetWeaponIdleTime(CurTime()+(seq==0 and 2 or 3))
end
weapons.Register(SWEP,"weapon_pipewrench")
