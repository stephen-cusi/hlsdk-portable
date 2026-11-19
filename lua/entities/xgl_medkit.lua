-- lua/entities/xgl_medkit.lua
-- M2 acceptance example (XGLua_Design.md §9.2 / §16).
-- NetworkVar is scheduled for M4; Charge is stored as an instance field in M2.

ENT.Type      = "anim"
ENT.Base      = "base_entity"
ENT.PrintName = "Medkit"
ENT.Author    = "XGLua"
ENT.Spawnable = true
ENT.Model     = "models/w_medkit.mdl"
ENT.PrecacheSounds = { "items/smallmedkit1.wav" }

function ENT:Initialize()
	self:SetModel( "models/w_medkit.mdl" )
	self:SetMoveType( MOVETYPE_TOSS )
	self:SetSolid( SOLID_TRIGGER )
	self:SetCollisionBounds( Vector( -8, -8, 0 ), Vector( 8, 8, 16 ) )
	self.Charge = 25
end

function ENT:Touch( other )
	if IsValid( other ) and other:IsPlayer() and other:Health() < other:GetMaxHealth() then
		local charge = self.Charge or 25
		other:SetHealth( math.min( other:GetMaxHealth(), other:Health() + charge ) )
		self:EmitSound( "items/smallmedkit1.wav" )
		self:Remove()
	end
end
