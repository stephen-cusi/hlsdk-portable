-- XGLua server-authoritative SWEP. Give with: give weapon_xgl_scatterpistol
-- Primary fires a spread of pellets; secondary fires one accurate shot.
-- Both modes use the same independently registered ammunition and clip.
SWEP.Base = "weapon_base"
SWEP.PrintName = "Scatter Pistol"
SWEP.Purpose = "A compact pistol with spread and focused fire modes."
SWEP.Instructions = "LMB: spread shot; RMB: focused shot; R: reload"
SWEP.Spawnable = true
SWEP.Slot = 1
SWEP.SlotPos = 4
SWEP.ViewModel = "models/v_9mmhandgun.mdl"
SWEP.WorldModel = "models/w_9mmhandgun.mdl"
SWEP.PlayerModel = "models/p_9mmhandgun.mdl"
SWEP.DeploySequence = 7
SWEP.AnimExtension = "onehanded"
SWEP.Primary = {
    ClipSize = 8,
    DefaultClip = 24,
    Ammo = "xgl_scatter",
    Automatic = false,
    Delay = 0.65
}
SWEP.Secondary = { Automatic = false, Delay = 0.4 }
SWEP.ReloadTime = 2
SWEP.ReloadAnimation = 5

function SWEP:Deploy()
    self:SetNextPrimaryFire(CurTime() + 0.4)
    self:SetNextSecondaryFire(CurTime() + 0.4)
    return true
end

local function fire(self, damage, count, spread, delay, recoil)
    local owner = self:GetOwner()
    if not IsValid(owner) or not owner:IsPlayer() or not owner:Alive() then return end
    if not self:CanPrimaryAttack() then return end

    -- ShootBullet is server-authoritative; it does not consume ammunition.
    self:ShootBullet(damage, count, Vector(spread, spread, 0))
    self:TakePrimaryAmmo(1)
    self:SendWeaponAnim(3)
    self:EmitSound("weapons/pl_gun3.wav", 0.8)
    owner:ViewPunch(Angle(recoil, 0, 0))

    -- Lock both modes so alternating buttons cannot bypass the fire delay.
    local nextFire = CurTime() + delay
    self:SetNextPrimaryFire(nextFire)
    self:SetNextSecondaryFire(nextFire)
end

function SWEP:PrimaryAttack()
    fire(self, 7, 5, 0.08, self.Primary.Delay, -3)
end

function SWEP:SecondaryAttack()
    fire(self, 20, 1, 0.015, self.Secondary.Delay, -1)
end

if SERVER then weapons.Register(SWEP, "weapon_xgl_scatterpistol") end
