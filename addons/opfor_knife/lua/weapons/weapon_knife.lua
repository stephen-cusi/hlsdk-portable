local cfg=includeAddon("opfor_knife","config.lua")
for _,model in ipairs({cfg.Weapon.ViewModel,cfg.Weapon.WorldModel,cfg.Weapon.PlayerModel}) do
 if not file.Exists(model) then print("[Knife] missing "..model); return end
end
for k,v in pairs(cfg.Weapon) do SWEP[k]=v end
SWEP.PrecacheSounds={}
for _,group in pairs(cfg.Sounds) do for _,sound in ipairs(group) do SWEP.PrecacheSounds[#SWEP.PrecacheSounds+1]=sound end end
local function sound(self,group) self:EmitSound(group[math.random(1,#group)]) end
function SWEP:PrimaryAttack()
 local now=CurTime()
 local damage=cfg.Damage
 if game.SinglePlayer() and self._lastSwing and now-self._lastSwing<1 then damage=damage/2 end
 self._lastSwing=now
 local result=self:TraceMeleeAttack(cfg.Range,damage)
 self:SendOwnerAttackAnimation()
 if result.Hit then
  self._swing=1-(self._swing or 0); self:SendWeaponAnim(self._swing==1 and 6 or 8)
  sound(self,result.HitFlesh and cfg.Sounds.Body or cfg.Sounds.Wall)
  self:SetNextAttackDelay(0.25)
 else
  self:SendWeaponAnim(math.random(0,1)==0 and 5 or 7); sound(self,cfg.Sounds.Miss)
  self:SetNextAttackDelay(0.5)
 end
 self:SetWeaponIdleTime(CurTime()+math.random(10,15))
end
function SWEP:Holster() self:SendWeaponAnim(2); self:SetNextAttack(0.5) end
function SWEP:WeaponIdle()
 if CurTime()<self:GetWeaponIdleTime() then return end
 local f=math.random(); local seq=f>0.9 and 9 or (f>0.5 and 0 or 10)
 self:SendWeaponAnim(seq); self:SetWeaponIdleTime(CurTime()+(seq==0 and 70/25 or 160/30))
end
weapons.Register(SWEP,"weapon_knife")
