-- addons/opfor_m249/lua/weapons/weapon_m249.lua
-- Opposing Force M249 Squad Automatic Weapon (independent server-authoritative addon)

local cfg = includeAddon("opfor_m249", "config.lua")
local REQUIRED_ASSETS = cfg.Required

for _, assetPath in ipairs( REQUIRED_ASSETS ) do
	if not file.Exists( assetPath ) then
		print( "[XGLua] weapon_m249: missing Opposing Force asset '" .. assetPath .. "', skipping registration" )
		return
	end
end

-- Register standalone 5.56mm belt ammo type (GMod style)
game.AddAmmoType( {
	name     = cfg.Weapon.Primary.Ammo,
	maxcarry = cfg.Weapon.Primary.MaxAmmo
} )

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

local CONE_6DEG = Vector( cfg.SpreadSingle, cfg.SpreadSingle, 0 )
local CONE_3DEG = Vector( cfg.SpreadMulti, cfg.SpreadMulti, 0 )

local FIRE_SOUNDS = cfg.Sounds.Fire
for key, value in pairs(cfg.Weapon) do SWEP[key] = value end
SWEP.PrecacheModels = {cfg.Models.Shell, cfg.Models.Link}
SWEP.PrecacheSounds = cfg.Sounds.Precache
SWEP.SpecialReload = cfg.Reload

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
	local owner = self:GetOwner()
	if not IsValid( owner ) then
		return
	end

	-- Opposing Force M249 cannot shoot when fully submerged.
	if owner:WaterLevel() == 3 then
		self:PlayEmptySound()
		self:SetNextPrimaryFire( CurTime() + 0.15 )
		return
	end
	if not self:CanPrimaryAttack() then
		self:SetNextPrimaryFire( CurTime() + 0.15 )
		return
	end

	self:TakePrimaryAmmo( 1 )
	local body = self:UpdateBodyFromClip()

	self:SendWeaponAnim( math.random( SEQ_SHOOT1, SEQ_SHOOT3 ), body )
	self:MuzzleFlash()
	self:SendOwnerAttackAnimation()
	self:EmitSound( FIRE_SOUNDS[ math.random( 1, #FIRE_SOUNDS ) ] )

	-- Match Opposing Force: six degrees in campaign, three in multiplayer.
	local spread = game.SinglePlayer() and CONE_6DEG or CONE_3DEG
	-- Default skillopfor.cfg value; dynamic skill cvars are not wired up yet.
	self:ShootBullet( cfg.Damage, 1, spread )

	-- Alternating shell ejection (right vs left/upward kick matching m249.cpp)
	self._AltEject = not self._AltEject
	if self._AltEject then
		self:EjectBrass( cfg.Models.Link, math.random( 90, 120 ), math.random( 90, 120 ), 18, 8, -6 )
	else
		self:EjectBrass( cfg.Models.Shell, math.random( -60, -35 ), math.random( 110, 140 ), 18, 4, -5 )
	end

	-- Overwrite punchangle + incremental horizontal pushback (matching m249.cpp)
	local pitchKick = 1.0 + math.random() * 0.5
	local yawKick   = -0.5 + math.random() * 0.3
	owner:SetViewPunch( Angle( pitchKick, yawKick, 0 ) )

	if owner:OnGround() then
		local aim  = owner:GetAimVector()
		local push = 40 + math.random( 1, 2 ) * 2
		owner:AddVelocity( Vector( -aim.x * push, -aim.y * push, 0 ) )
	end

	-- The bridge converts the engine weapon clock to an absolute SWEP deadline.
	self:SetNextAttackDelay( cfg.FireDelay )
	self:SetWeaponIdleTime( CurTime() + 0.5 )
end

function SWEP:SecondaryAttack()
	-- M249 has no secondary fire in Opposing Force
end

weapons.Register( SWEP, "weapon_m249" )
