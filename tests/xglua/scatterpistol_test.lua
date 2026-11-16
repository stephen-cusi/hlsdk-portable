-- Run from repository root with LuaJIT. Engine methods are test doubles.
local script = (arg[0] or ""):gsub("\\", "/")
local root = script:match("^(.-)tests/xglua/[^/]+$")
assert(root ~= nil, "Run this test as tests/xglua/scatterpistol_test.lua")
if root == "" then root = "./" end

dofile(root .. "lua/includes/modules/weapons.lua")
SERVER = true
SWEP = {}
dofile(root .. "lua/weapons/weapon_xgl_scatterpistol.lua")
local definition = assert(weapons.GetStored("weapon_xgl_scatterpistol"))
assert(definition.Base == "weapon_base")
assert(definition.Primary.Ammo == "xgl_scatter" and definition.Primary.ClipSize == 8)
assert(definition.PlayerModel == "models/p_9mmhandgun.mdl")

local time = 100
CurTime = function() return time end
IsValid = function(value) return value ~= nil end
Vector = function(x, y, z) return {x = x, y = y, z = z} end
Angle = function(p, y, r) return {p = p, y = y, r = r} end
local owner = {
    IsPlayer = function() return true end,
    Alive = function() return true end,
    GetAmmoCount = function() return 16 end,
    ViewPunch = function(self, a) self.punch = a end
}
local instance = {
    clip = 8,
    GetOwner = function(self) return self.owner end,
    Clip1 = function(self) return self.clip end,
    ShootBullet = function(self, damage, count, spread)
        self.shot = {damage = damage, count = count, spread = spread}
        self.shotCount = (self.shotCount or 0) + 1
    end,
    TakePrimaryAmmo = function(self, amount) self.clip = self.clip - amount end,
    SendWeaponAnim = function(self, anim) self.anim = anim end,
    EmitSound = function(self, sound) self.sound = sound end,
    SetNextPrimaryFire = function(self, value) self.nextPrimary = value end,
    SetNextSecondaryFire = function(self, value) self.nextSecondary = value end,
    PlayEmptySound = function(self) self.empty = true end,
    DefaultReload = function(self, size, anim, delay)
        self.reload = {size, anim, delay}
        return true
    end
}
setmetatable(instance, {__index = definition})
instance.owner = owner
instance:Deploy()
assert(instance.nextPrimary == 100.4 and instance.nextSecondary == 100.4)
instance:PrimaryAttack()
assert(instance.clip == 7 and instance.shot.damage == 7 and instance.shot.count == 5)
assert(instance.shot.spread.x == 0.08 and instance.shot.spread.y == 0.08)
assert(instance.nextPrimary == 100.65 and instance.nextSecondary == 100.65)
assert(owner.punch.p == -3 and instance.anim == 3)
instance:SecondaryAttack()
assert(instance.clip == 6 and instance.shot.damage == 20 and instance.shot.count == 1)
assert(instance.shot.spread.x == 0.015 and instance.shot.spread.y == 0.015)
assert(instance.nextPrimary == 100.4 and instance.nextSecondary == 100.4 and owner.punch.p == -1)
assert(instance:Reload())
assert(instance.reload[1] == 8 and instance.reload[2] == 5 and instance.reload[3] == 2)
instance.clip = 0
instance:PrimaryAttack()
assert(instance.empty and instance.shotCount == 2 and instance.clip == 0)
assert(instance.nextPrimary == 100.2)
instance.owner = nil
instance.clip = 8
instance:PrimaryAttack()
assert(instance.shotCount == 2 and instance.clip == 8)
owner.Alive = function() return false end
instance.owner = owner
instance:SecondaryAttack()
assert(instance.shotCount == 2 and instance.clip == 8)
print("scatter pistol example tests passed")
