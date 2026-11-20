-- Run from the repo root with the project LuaJIT.
dofile("lua/includes/modules/capabilities.lua")
dofile("lua/includes/modules/weapons.lua")
capabilities.Register("xglua.weapons", 1, weapons)
SWEP = {}
dofile("lua/weapons/weapon_xgl_template.lua")
local helper = { Add = function(a, b) return a + b end }
capabilities.Register("example.weapon_helper", 2, helper)
assert(capabilities.Get("example.weapon_helper", 1) == helper)
assert(capabilities.Get("example.weapon_helper", 2) == helper)
assert(capabilities.Get("example.weapon_helper", 3) == nil)
assert(not capabilities.Has("example.sandbox_only"))
assert(not pcall(capabilities.Register, "example.weapon_helper", 2, helper))
for _, mode in ipairs({ "campaign", "deathmatch", "sandbox" }) do
    GAMEMODE = { Name = mode }
    assert(capabilities.Get("xglua.weapons").Get("weapon_xgl_template"))
    local api = capabilities.Get("example.weapon_helper", 1)
    assert(api and api.Add(2, 3) == 5)
end
print("capability lookup across modes OK")
