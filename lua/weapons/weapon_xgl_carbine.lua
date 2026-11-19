-- lua/weapons/weapon_xgl_carbine.lua
-- M5 P1 server-authoritative SWEP (fixed anim sequences, reload gate & sound).

SWEP.PrintName      = "XGLua Carbine"
SWEP.Slot           = 2
SWEP.SlotPos        = 2
SWEP.ViewModel      = "models/v_9mmAR.mdl"
SWEP.WorldModel     = "models/w_9mmAR.mdl"
SWEP.PlayerModel    = "models/p_9mmAR.mdl"
SWEP.DeploySequence = 4
SWEP.AnimExtension  = "mp5"
SWEP.PrecacheSounds = { "weapons/hks1.wav", "weapons/hks2.wav", "items/clipinsert1.wav" }

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
	self.FireSeq1     = 5
	self.FireSeq2     = 6
	self.ReloadSeq    = 3
	self.ReloadTime   = 1.5
	self.IdleSeq      = 1
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

function SWEP:SecondaryAttack()
	if not self:CanPrimaryAttack() then return end

	self:SetNextSecondaryFire( CurTime() + 0.35 )
	self:SetNextPrimaryFire( CurTime() + 0.35 )
	self:SetWeaponIdleTime( CurTime() + 0.55 )
	local shots = math.min( 3, self:Clip1() )
	if shots <= 0 then return end

	self:TakePrimaryAmmo( shots )
	self:ShootBullet( 10, shots, Vector( 0.04, 0.04, 0 ) )
	self:SendWeaponAnim( self.FireSeq2 )
	self:MuzzleFlash()
	self:SendOwnerAttackAnimation()
	self:EmitSound( "weapons/hks2.wav" )

	local owner = self:GetOwner()
	if IsValid( owner ) and owner:IsPlayer() then
		owner:ViewPunch( Angle( -3.5, 0, 0 ) )
	end
end

function SWEP:Reload()
	local maxClip = self:GetMaxClip1()
	if maxClip <= 0 or self:Clip1() >= maxClip then return end
	if self:Ammo1() <= 0 then return end

	if self:DefaultReload( maxClip, self.ReloadSeq, self.ReloadTime ) then
		self:SetWeaponIdleTime( CurTime() + self.ReloadTime + 0.25 )
		-- v_9mmAR 的换弹动画自带 clipinsert1/cliprelease1 音效事件,
		-- 脚本不再手动 EmitSound, 否则会双重播放
	end
end

function SWEP:WeaponIdle()
	if self:GetWeaponIdleTime() > CurTime() then return end
	self:SendWeaponAnim( self.IdleSeq )
	self:SetWeaponIdleTime( CurTime() + 4.0 )
end

weapons.Register( SWEP, "weapon_xgl_carbine" )
