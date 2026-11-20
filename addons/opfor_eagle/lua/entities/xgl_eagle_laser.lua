local cfg = includeAddon("opfor_eagle", "config.lua")
ENT.Type="anim"; ENT.Base="base_entity"; ENT.Model=cfg.Laser.Model
ENT.Abstract=true
if not file.Exists(ENT.Model) then print("[Eagle] laser sprite missing"); return end
ENT.Abstract=false
function ENT:Initialize()
 self:SetSolid(SOLID_NOT); self:SetMoveType(MOVETYPE_NONE)
 self:SetRenderMode(3); self:SetRenderFX(14); self:SetRenderAmount(255); self:SetModelScale(0.5)
end
function ENT:Think()
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
