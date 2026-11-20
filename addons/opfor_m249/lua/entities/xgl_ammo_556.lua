local cfg = includeAddon("opfor_m249", "config.lua")
local pickup, primary = cfg.AmmoPickup, cfg.Weapon.Primary
-- Opposing Force M249 ammunition: 50 rounds per box.
ENT.Type="anim"; ENT.Base="base_entity"; ENT.PrintName="M249 5.56mm belt"
ENT.Spawnable=true; ENT.Abstract=true
if not file.Exists(pickup.Model) or not file.Exists("sound/" .. pickup.Sound) then
 print("[XGLua] xgl_ammo_556: pickup assets missing"); return
end
ENT.Abstract=false; ENT.Model=pickup.Model; ENT.PrecacheSounds={pickup.Sound}
function ENT:Initialize()
 self:SetModel(self.Model); self:SetMoveType(MOVETYPE_TOSS); self:SetSolid(SOLID_TRIGGER)
 self:SetCollisionBounds(Vector(-16,-16,0),Vector(16,16,16))
end
function ENT:Touch(other)
 if not IsValid(other) or not other:IsPlayer() then return end
 local before=other:GetAmmoCount(primary.Ammo); if before>=primary.MaxAmmo then return end
 other:GiveAmmo(pickup.Amount,primary.Ammo,primary.MaxAmmo)
 if other:GetAmmoCount(primary.Ammo)>before then self:EmitSound(pickup.Sound); self:Remove() end
end
