-- A server-side grenade launcher built on the shared Lua weapon base.
SWEP.Base = "weapon_base"
SWEP.PrintName = "Grenade Pistol"
SWEP.Slot = 2
SWEP.SlotPos = 1
SWEP.Spawnable = true
SWEP.ViewModel = "models/v_9mmhandgun.mdl"
SWEP.WorldModel = "models/w_9mmhandgun.mdl"
SWEP.Primary = {
    ClipSize = 6,
    DefaultClip = 18,
    Ammo = "xgl_grenade",
    Automatic = false,
    Delay = 0.5
}
SWEP.Secondary = { Automatic = false, Delay = 0.7 }
SWEP.ReloadTime = 2
SWEP.ReloadAnimation = 5

function SWEP:Deploy()
    self:SetNextPrimaryFire(CurTime() + 0.4)
    self:SetNextSecondaryFire(CurTime() + 0.4)
    return true
end

local function fire(self, contact, speed, delay)
    local owner = self:GetOwner()
    if not IsValid(owner) or not owner:IsPlayer() or not owner:Alive() then return end
    if not self:CanPrimaryAttack() then return end
    local aim = owner:GetAimVector()
    local grenade = util.ThrowGrenade(owner, owner:EyePos() + aim * 16,
        aim * speed + Vector(0, 0, 100), contact)
    if not IsValid(grenade) then return end
    self:TakePrimaryAmmo(1)
    self:SendWeaponAnim(3)
    self:EmitSound("weapons/pl_gun3.wav", 0.8)
    local nextFire = CurTime() + delay
    self:SetNextPrimaryFire(nextFire)
    self:SetNextSecondaryFire(nextFire)
end

function SWEP:PrimaryAttack()
    fire(self, true, 900, self.Primary.Delay)
end

function SWEP:SecondaryAttack()
    fire(self, false, 700, self.Secondary.Delay)
end

if SERVER then weapons.Register(SWEP, "weapon_grenadepistol") end
