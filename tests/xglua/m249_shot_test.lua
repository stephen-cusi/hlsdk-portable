function includeAddon(name,path) return assert(loadfile("../../addons/"..name.."/lua/"..path))() end
local modeSingle = true
local lastDamage, lastSpread, lastDelay, kick, impulse
local ejected = {}
local water = 0
local emptySounds = 0
local nextFire = nil
SERVER = true
SWEP = {}
file = { Exists = function() return true end }
game = { AddAmmoType = function() end, SinglePlayer = function() return modeSingle end }
Vector = function(x,y,z) return {x=x,y=y,z=z} end
Angle = Vector
CurTime = function() return 10 end
IsValid = function(v) return v ~= nil end
scripted_ents = { Register = function() end }
weapons = { Register = function(s, name) assert(name == 'weapon_m249') end }
assert(loadfile('../../addons/opfor_m249/lua/weapons/weapon_m249.lua'))()
local owner = {
  SetViewPunch = function(_, a) kick = a end,
  OnGround = function() return true end,
  WaterLevel = function() return water end,
  GetAimVector = function() return Vector(1,0,0.4) end,
  AddVelocity = function(_, v) impulse = v end
}
local w = setmetatable({}, {__index = SWEP})
function w:CanPrimaryAttack() return true end
function w:GetOwner() return owner end
function w:TakePrimaryAmmo(n) assert(n == 1) end
function w:UpdateBodyFromClip() return 0 end
function w:SendWeaponAnim() end
function w:MuzzleFlash() end
function w:SendOwnerAttackAnimation() end
function w:EmitSound() end
function w:ShootBullet(d, n, s) lastDamage, lastSpread = d, s; assert(n == 1) end
function w:EjectBrass(model) ejected[#ejected + 1] = model end
function w:PlayEmptySound() emptySounds = emptySounds + 1 end
function w:SetNextPrimaryFire(t) nextFire = t end
function w:SetNextAttackDelay(d) lastDelay = d end
function w:SetWeaponIdleTime() end
for _, single in ipairs({true, false}) do
  modeSingle = single
  w:PrimaryAttack()
  assert(lastDamage == 15)
  assert(math.abs(lastSpread.x - (single and 0.05234 or 0.02618)) < 0.00001)
  assert(lastDelay == 0.067)
  assert(kick.x >= 1 and kick.x <= 1.5 and kick.y >= -0.5 and kick.y <= -0.2)
  assert((impulse.x == -42 or impulse.x == -44) and impulse.y == 0 and impulse.z == 0)
end
water = 3
lastDamage = nil
w:PrimaryAttack()
assert(lastDamage == nil and emptySounds == 1 and nextFire == 10.15)
assert(ejected[1] == 'models/saw_link.mdl' and ejected[2] == 'models/saw_shell.mdl')
print('M249 spread, damage, cadence request, recoil and underwater guard: PASS')
