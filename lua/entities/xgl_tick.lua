-- lua/entities/xgl_tick.lua
-- ENT:Think cadence smoke entity (M2 acceptance #3).
-- Think is defined at CLASS level so BuildEntityCache sees hasThink=true
-- and Spawn arms the 0.1s default cadence.

ENT.Type      = "anim"
ENT.Base      = "base_entity"
ENT.PrintName = "Tick"
ENT.Spawnable = true

function ENT:Initialize()
	self:SetModel( "models/w_medkit.mdl" )
	self:SetMoveType( MOVETYPE_NOCLIP )
	self:SetSolid( SOLID_NOT )
	self.Ticks = 0
	self.Interval = 0.5
end

function ENT:Think()
	self.Ticks = ( self.Ticks or 0 ) + 1
	print( "[xgl_tick] think tick " .. self.Ticks )
	if self.Ticks >= 5 then
		print( "[xgl_tick] 5 ticks reached, self-removing (Remove-in-Think guard)" )
		self:Remove()
		return
	end
	-- script-set cadence must win over the 0.1s default re-arm
	self:SetNextThink( CurTime() + self.Interval )
end
