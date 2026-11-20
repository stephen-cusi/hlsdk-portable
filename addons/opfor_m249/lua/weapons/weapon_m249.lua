-- addons/opfor_m249/lua/weapons/weapon_m249.lua
-- Opposing Force M249 Squad Automatic Weapon (independent server-authoritative addon)

local REQUIRED_ASSETS = {
	"models/v_saw.mdl",
	"models/w_saw.mdl",
	"models/p_saw.mdl",
	"models/saw_shell.mdl",
	"sound/weapons/saw_fire1.wav",
	"sound/weapons/saw_fire2.wav",
	"sound/weapons/saw_fire3.wav",
	"sound/weapons/saw_reload.wav",
	"sound/weapons/saw_reload2.wav"
}

for _, assetPath in ipairs( REQUIRED_ASSETS ) do
	if not file.Exists( assetPath ) then
		print( "[XGLua] weapon_m249: missing Opposing Force asset '" .. assetPath .. "', skipping registration" )
		return
	end
end

-- Sequence table verified from gearbox/models/v_saw.mdl (activity=0 on all sequences)
local SEQ_SLOWIDLE = 0
local SEQ_IDLE2    = 1
local SEQ_LAUNCH   = 2 -- open cover / clear belt
local SEQ_RELOAD1  = 3 -- close cover / seat belt
local SEQ_HOLSTER  = 4
local SEQ_DRAW     = 5
local SEQ_SHOOT1   = 6
local SEQ_SHOOT2   = 7
local SEQ_SHOOT3   = 8

local CONE_6DEG = Vector( 0.05234, 0.05234, 0 )
local CONE_3DEG = Vector( 0.02618, 0.02618, 0 )

local FIRE_SOUNDS = {
	"weapons/saw_fire1.wav",
	"weapons/saw_fire2.wav",
	"weapons/saw_fire3.wav"
}

SWEP.PrintName      = "M249 SAW"
SWEP.ViewModel      = "models/v_saw.mdl"
SWEP.WorldModel     = "models/w_saw.mdl"
SWEP.PlayerModel    = "models/p_saw.mdl"
SWEP.DeploySequence = SEQ_DRAW
SWEP.AnimExtension  = "mp5"

SWEP.Slot           = 2
SWEP.SlotPos        = 2
SWEP.Weight         = 15

SWEP.Primary = {
	ClipSize    = 50,
	DefaultClip = 50,
	MaxAmmo     = 200,
	Ammo        = "556",
	Automatic   = true
}

SWEP.Secondary = {
	ClipSize  = -1,
	Ammo      = "none",
	Automatic = false
}

SWEP.PrecacheModels = {
	"models/saw_shell.mdl"
}

SWEP.PrecacheSounds = {
	"weapons/saw_fire1.wav",
	"weapons/saw_fire2.wav",
	"weapons/saw_fire3.wav",
	"weapons/saw_reload.wav",
	"weapons/saw_reload2.wav"
}

-- Two-stage belt-fed reload driven by WeaponBase:ThinkSpecialReload
SWEP.SpecialReload = {
	OpenAnim   = SEQ_LAUNCH,
	OpenDelay  = 1.33,
	CloseAnim  = SEQ_RELOAD1,
	CloseDelay = 1.0
}

SWEP.IdleSeq       = SEQ_SLOWIDLE
SWEP.LongIdleSeq   = SEQ_IDLE2
SWEP.LongIdleEvery = 3

function SWEP:BodyFromClip( clip )
	if clip <= 0 then return 8 end
	if clip <= 8 then return 9 - clip end
	return 0
end

function SWEP:Initialize()
	self._AltEject = false
	self:UpdateBodyFromClip()
end

function SWEP:Deploy()
	local body = self:UpdateBodyFromClip()
	self:SendWeaponAnim( SEQ_DRAW, body )
	self:SetWeaponIdleTime( CurTime() + 1.0 )
end

function SWEP:PrimaryAttack()
	if not self:CanPrimaryAttack() then
		return
	end

	local owner = self:GetOwner()
	if not IsValid( owner ) then
		return
	end

	self:TakePrimaryAmmo( 1 )
	local body = self:UpdateBodyFromClip()

	self:SendWeaponAnim( math.random( SEQ_SHOOT1, SEQ_SHOOT3 ), body )
	self:MuzzleFlash()
	self:SendOwnerAttackAnimation()
	self:EmitSound( FIRE_SOUNDS[ math.random( 1, #FIRE_SOUNDS ) ] )

	local spread = owner:Crouching() and CONE_3DEG or CONE_6DEG
	self:ShootBullet( 18, 1, spread )

	-- Alternating shell ejection (right vs left/upward kick matching m249.cpp)
	self._AltEject = not self._AltEject
	if self._AltEject then
		self:EjectBrass( "models/saw_shell.mdl", math.random( 90, 120 ), math.random( 90, 120 ), 18, 8, -6 )
	else
		self:EjectBrass( "models/saw_shell.mdl", math.random( -60, -35 ), math.random( 110, 140 ), 18, 4, -5 )
	end

	-- View punch + ground recoil pushback
	local pitchKick = -( 1.0 + math.random() * 0.5 )
	local yawKick   = -0.5 + math.random() * 0.3
	owner:ViewPunch( Angle( pitchKick, yawKick, 0 ) )

	if owner:OnGround() then
		local aim = owner:GetAimVector()
		local vel = owner:GetVelocity()
		owner:SetVelocity( Vector( vel.x - aim.x * 35, vel.y - aim.y * 35, vel.z ) )
	end

	local nextDelay = self:GetNextAttackDelay( 0.067 )
	self:SetNextPrimaryFire( CurTime() + nextDelay )
	self:SetWeaponIdleTime( CurTime() + 0.5 )
end

function SWEP:SecondaryAttack()
	-- M249 has no secondary fire in Opposing Force
end

weapons.Register( SWEP, "weapon_m249" )
