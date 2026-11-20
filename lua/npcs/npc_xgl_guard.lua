-- lua/npcs/npc_xgl_guard.lua
-- M7-7D.1 polished Lua NPC example using Activity lookup (no hardcoded sequence IDs).

NPC.Model      = "models/barney.mdl"
NPC.Health     = 120
NPC.YawSpeed   = 200
NPC.Classify   = 3 -- CLASS_HUMAN_MILITARY

NPC.PrecacheSounds = {
	"barney/ba_attack2.wav",
	"barney/ba_pain1.wav",
	"barney/ba_die1.wav"
}

function NPC:Initialize()
	self:DropToFloor()
	self:SetActivity( ACT_IDLE )
	self.NextAttackTime = 0
	self.FlinchUntil = 0
end

function NPC:Think()
	if CurTime() < ( self.FlinchUntil or 0 ) then
		return 0.1
	end

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
		self:SetActivity( ACT_IDLE )
		return 0.25
	end

	local myPos = self:GetPos()
	local targetPos = enemy:GetPos()
	local dist = myPos:Distance( targetPos )

	self:TurnToward( targetPos )

	if dist > 72 then
		self:SetActivity( ACT_RUN )
		self:MoveToPos( targetPos, 24 )
	else
		if CurTime() >= ( self.NextAttackTime or 0 ) then
			self.NextAttackTime = CurTime() + 1.0
			self:SetActivity( ACT_MELEE_ATTACK1 )
			self:EmitSound( "barney/ba_attack2.wav" )
			enemy:SetHealth( math.max( 0, enemy:Health() - 10 ) )
			if enemy:Health() <= 0 then
				enemy:Kill()
			end
		elseif self:IsSequenceFinished() then
			self:SetActivity( ACT_IDLE )
		end
	end

	return 0.1
end

function NPC:OnTakeDamage( attacker, inflictor, damage, dmgType )
	if IsValid( attacker ) then
		self:SetEnemy( attacker )
	end
	self:EmitSound( "barney/ba_pain1.wav" )
	self:SetActivity( ACT_SMALL_FLINCH )
	self.FlinchUntil = CurTime() + 0.35
end

function NPC:OnKilled( attacker, inflictor )
	self:EmitSound( "barney/ba_die1.wav" )
	-- Killed() already triggers ACT_DIESIMPLE; override here if a custom death anim is desired
end

npc.Register( NPC, "npc_xgl_guard" )
