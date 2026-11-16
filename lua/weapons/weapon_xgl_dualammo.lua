-- Server-authoritative XGLua example. Give with: give weapon_xgl_dualammo
-- Primary: automatic bullets from a clip; secondary: contact grenade reserve.
-- This uses GoldSrc's player, bullet, grenade and animation mechanisms.
SWEP.Base = "weapon_base"
SWEP.PrintName = "Dual Ammo SMG"
SWEP.Purpose = "An SMG with a separate reserve for contact grenades."
SWEP.Instructions = "LMB: automatic fire; RMB: contact grenade; R: reload"
SWEP.Spawnable = true
SWEP.Slot = 2
SWEP.SlotPos = 2
SWEP.ViewModel = "models/v_9mmAR.mdl"
SWEP.WorldModel = "models/w_9mmAR.mdl"
SWEP.PlayerModel = "models/p_9mmAR.mdl"
SWEP.DeploySequence = 4
SWEP.AnimExtension = "mp5"
-- GoldSrc sound paths to precache while the map is loading.
SWEP.PrecacheSounds = { "weapons/hks1.wav", "weapons/glauncher.wav" }
SWEP.Primary = {
    ClipSize = 30,
    DefaultClip = 90,
    Ammo = "xgl_dual_round",
    Automatic = true,
    Delay = 0.12
}
SWEP.Secondary = {
    ClipSize = -1,
    Ammo = "xgl_dual_grenade",
    DefaultAmmo = 4,
    MaxAmmo = 10,
    Automatic = false,
    Delay = 1.0
}
SWEP.ReloadAnimation = 3
SWEP.ReloadTime = 1.5

function SWEP:Deploy()
    local ready = CurTime() + 0.5
    self:SetNextPrimaryFire(ready)
    self:SetNextSecondaryFire(ready)
    self:SetWeaponIdleTime(CurTime() + 3)
    return true
end

local function validOwner(self)
    local owner = self:GetOwner()
    if IsValid(owner) and owner:IsPlayer() and owner:Alive() then return owner end
end

function SWEP:PrimaryAttack()
    local owner = validOwner(self)
    if not owner or not self:CanPrimaryAttack() then return end
    self:ShootBullet(8, 1, Vector(0.04, 0.04, 0))
    self:TakePrimaryAmmo(1)
    self:SendWeaponAnim(5)
    self:MuzzleFlash()
    self:SendOwnerAttackAnimation()
    self:EmitSound("weapons/hks1.wav", 0.8)
    owner:ViewPunch(Angle(-1, 0, 0))
    local nextFire = CurTime() + self.Primary.Delay
    self:SetNextPrimaryFire(nextFire)
    self:SetNextSecondaryFire(nextFire)
    self:SetWeaponIdleTime(CurTime() + 5)
end

function SWEP:SecondaryAttack()
    local owner = validOwner(self)
    if not owner or not self:CanSecondaryAttack() then return end
    local aim = owner:GetAimVector()
    local grenade = util.ThrowGrenade(owner, owner:EyePos() + aim * 16, aim * 800, true)
    if not IsValid(grenade) then return end
    self:TakeSecondaryAmmo(1)
    self:SendWeaponAnim(2)
    self:SendOwnerAttackAnimation()
    self:EmitSound("weapons/glauncher.wav", 0.8)
    local nextFire = CurTime() + self.Secondary.Delay
    self:SetNextPrimaryFire(nextFire)
    self:SetNextSecondaryFire(nextFire)
    self:SetWeaponIdleTime(CurTime() + 5)
end

function SWEP:WeaponIdle()
    if self:GetWeaponIdleTime() > CurTime() then return end
    self:SendWeaponAnim(0)
    self:SetWeaponIdleTime(CurTime() + 10)
end

if SERVER then weapons.Register(SWEP, "weapon_xgl_dualammo") end
