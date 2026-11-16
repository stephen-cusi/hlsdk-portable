-- Run from the repository root: luajit tests/xglua/weapon_base_test.lua
-- These tests stub engine bindings; they do not replace an in-engine test.
local script = (arg[0] or ""):gsub("\\", "/")
local root = script:match("^(.-)tests/xglua/[^/]+$")
assert(root ~= nil, "Run this test as tests/xglua/weapon_base_test.lua")
if root == "" then root = "./" end

dofile(root .. "lua/includes/modules/weapons.lua")
local base = assert(weapons.GetStored("weapon_base"))
assert(base.Abstract)
local child = { Base = "later_base" }
weapons.Register(child, "test_child")
weapons.Register({ Base = "weapon_base", Custom = 42 }, "later_base")
assert(child.Custom == 42 and child.BaseClass == weapons.GetStored("later_base"))
assert(not child.Abstract and type(child.CanPrimaryAttack) == "function")
weapons.Register({ Base = "test_cycle_b" }, "test_cycle_a")
weapons.Register({ Base = "test_cycle_a" }, "test_cycle_b")
assert(weapons.GetStored("test_cycle_a").Missing == nil)

SERVER = true
SWEP = {}
dofile(root .. "lua/weapons/weapon_grenadepistol.lua")
local weapon = assert(weapons.GetStored("weapon_grenadepistol"))
assert(weapon.Base == "weapon_base" and weapon.Primary.ClipSize == 6)

local vecmt = {}
vecmt.__add = function() return setmetatable({}, vecmt) end
vecmt.__mul = vecmt.__add
local function vector() return setmetatable({}, vecmt) end
Vector = vector
CurTime = function() return 100 end
IsValid = function(value) return value ~= nil end
local owner = {
    IsPlayer = function() return true end,
    Alive = function() return true end,
    GetAimVector = vector,
    EyePos = vector,
    GetAmmoCount = function() return 3 end
}
local contact, shouldSpawn = nil, true
util = { ThrowGrenade = function(_, _, _, c)
    contact = c
    if shouldSpawn then return {} end
end }
local instance = {
    clip = 6,
    GetOwner = function() return owner end,
    Clip1 = function(self) return self.clip end,
    TakePrimaryAmmo = function(self, count) self.clip = self.clip - count end,
    SendWeaponAnim = function() end,
    EmitSound = function() end,
    SetNextPrimaryFire = function(self, time) self.nextPrimary = time end,
    SetNextSecondaryFire = function(self, time) self.nextSecondary = time end,
    PlayEmptySound = function(self) self.playedEmpty = true end,
    DefaultReload = function(self, size, anim, delay)
        self.reload = { size, anim, delay }
        return true
    end
}
setmetatable(instance, { __index = weapon })
instance:PrimaryAttack()
assert(contact == true and instance.clip == 5 and instance.nextPrimary == 100.5)
instance:SecondaryAttack()
assert(contact == false and instance.clip == 4 and instance.nextSecondary == 100.7)
shouldSpawn = false
instance:PrimaryAttack()
assert(instance.clip == 4, "failed grenade spawn must not consume ammunition")
assert(instance:Reload())
assert(instance.reload[1] == 6 and instance.reload[2] == 5 and instance.reload[3] == 2)
instance.clip = 0
assert(not instance:CanPrimaryAttack() and instance.playedEmpty and instance.nextPrimary == 100.2)

local noClip = {
    Primary = { ClipSize = -1, Ammo = "test" },
    GetOwner = function() return owner end,
    Clip1 = function() return -1 end,
    PlayEmptySound = function(self) self.empty = true end,
    SetNextPrimaryFire = function(self, time) self.next = time end
}
setmetatable(noClip, { __index = base })
assert(noClip:CanPrimaryAttack())
owner.GetAmmoCount = function() return 0 end
assert(not noClip:CanPrimaryAttack() and noClip.empty and noClip.next == 100.2)
noClip.Primary.Ammo = "none"
assert(noClip:CanPrimaryAttack())
assert(not noClip:Reload())
print("weapon base and grenade Lua tests passed")
