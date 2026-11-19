-- lua/weapons/weapon_xgl_template.lua
-- XGLua SWEP (P1 server-authoritative) template.
-- Contracts:
--   1. Explicitly call weapons.Register(SWEP, "<classname>") at the bottom.
--   2. List every sound used by self:EmitSound(...) in SWEP.PrecacheSounds.
--   3. WeaponIdle alternates idle1/longidle via WeaponBase defaults
--   4. Sequence numbers are model-specific; verify with xgl_dump_model
--      <model path> before shipping (v_9mmAR.mdl table below).

SWEP.PrintName      = "XGLua Template Weapon"
SWEP.Slot           = 2
SWEP.SlotPos        = 3
SWEP.ViewModel      = "models/v_9mmAR.mdl"
SWEP.WorldModel     = "models/w_9mmAR.mdl"
SWEP.PlayerModel    = "models/p_9mmAR.mdl"
SWEP.DeploySequence = 4 -- v_9mmAR: 0 longidle, 1 idle1, 2 grenade, 3 reload, 4 deploy, 5-7 shoot
SWEP.AnimExtension  = "mp5"

SWEP.PrecacheSounds = {
	"weapons/hks1.wav",
	"items/clipinsert1.wav"
}

SWEP.Primary = {
	ClipSize    = 30,
	DefaultClip = 60,
	Ammo        = "9mm",
	Automatic   = true
}

SWEP.Secondary = {
	ClipSize    = -1,
	DefaultClip = 0,
	Ammo        = "none",
	Automatic   = false
}

function SWEP:Initialize()
	self.FireSeq1    = 5
	self.FireSeq2    = 6
	self.ReloadSeq   = 3
	self.IdleSeq     = 1
	self.LongIdleSeq    = 0
	self.LongIdleEvery  = 3
	self.IdleMinDelay   = 3.5
	self.IdleMaxDelay   = 6.0
end

function SWEP:PrimaryAttack()
	if not self:CanPrimaryAttack() then return end

	self:SetNextPrimaryFire( CurTime() + 0.12 )
	self:SetWeaponIdleTime( CurTime() + 0.35 )
	self:TakePrimaryAmmo( 1 )
	self:ShootBullet( 12, 1, Vector( 0.015, 0.015, 0 ) )
	self:SendWeaponAnim( self.FireSeq1 )
	self:MuzzleFlash()
	self:SendOwnerAttackAnimation()
	self:EmitSound( "weapons/hks1.wav" )

	local owner = self:GetOwner()
	if IsValid( owner ) and owner:IsPlayer() then
		owner:ViewPunch( Angle( -1.5, 0, 0 ) )
	end
end

function SWEP:Reload()
	local maxClip = self:GetMaxClip1()
	if maxClip <= 0 or self:Clip1() >= maxClip then return end
	if self:Ammo1() <= 0 then return end

	if self:DefaultReload( maxClip, self.ReloadSeq, self.ReloadTime ) then
		self:SetWeaponIdleTime( CurTime() + self.ReloadTime + 0.25 )
		if self.ReloadSound and self.ReloadSound ~= "" then
			self:EmitSound( self.ReloadSound )
		end
	end
end


weapons.Register( SWEP, "weapon_xgl_template" )
