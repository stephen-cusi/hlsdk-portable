-- lua/npcs/npc_xgl_guard.lua
-- M7-7D simplified Lua NPC example.

NPC.Model      = "models/barney.mdl"
NPC.Health     = 120
NPC.YawSpeed   = 200
NPC.Classify   = 3 -- CLASS_HUMAN_MILITARY

NPC.PrecacheSounds = {
	"barney/ba_attack2.wav",
	"barney/ba_die1.wav"
}

function NPC:Initialize()
	self:DropToFloor()
	self:SetSequence( 0 ) -- idle
	self.NextAttackTime = 0
end

function NPC:Think()
	local enemy = self:GetEnemy()
	if not IsValid( enemy ) or not enemy:Alive() then
		enemy = nil
		for _, ply in ipairs( player.GetAll() ) do
			if IsValid( ply ) and ply:Alive() and self:CanSeeEntity( ply ) then
				enemy = ply
				self:SetEnemy( ply )
				break
			end
		end
	end

	if not enemy then
		self:SetSequence( 0 )
		return 0.25
	end

	local myPos = self:GetPos()
	local targetPos = enemy:GetPos()
	local dist = myPos:Distance( targetPos )

	self:TurnToward( targetPos )

	if dist > 72 then
		self:SetSequence( 3 ) -- run / walk
		self:MoveToPos( targetPos, 24 )
	else
		self:SetSequence( 0 )
		if CurTime() >= ( self.NextAttackTime or 0 ) then
			self.NextAttackTime = CurTime() + 1.0
			self:EmitSound( "barney/ba_attack2.wav" )
			enemy:SetHealth( math.max( 0, enemy:Health() - 10 ) )
			if enemy:Health() <= 0 then
				enemy:Kill()
			end
		end
	end

	return 0.1
end

function NPC:OnTakeDamage( attacker, inflictor, damage, dmgType )
	if IsValid( attacker ) then
		self:SetEnemy( attacker )
	end
end

function NPC:OnKilled( attacker, inflictor )
	self:EmitSound( "barney/ba_die1.wav" )
	self:SetSequence( 15 ) -- death sequence
end

npc.Register( NPC, "npc_xgl_guard" )
