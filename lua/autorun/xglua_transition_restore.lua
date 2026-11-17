-- Manually run in each map with:
--   lua_run include("tests/xglua_transition_restore.lua")
-- This report-only script never gives or removes weapons.
if not SERVER then return end

local classes = {
    { "weapon_xgl_scatterpistol", "xgl_scatter" },
    { "weapon_xgl_dualammo", "xgl_dual_round", "xgl_dual_grenade" },
    { "weapon_grenadepistol", "xgl_grenade" }
}

local player = Entity( 1 )
if not IsValid( player ) or not player:IsPlayer() then
    print( "[XGLua transition] player Entity(1) is unavailable" )
    return
end

local active = player:GetActiveWeapon()
print( "[XGLua transition] map report; active=" ..
    ( IsValid( active ) and active:GetClass() or "none" ) )

for _, spec in ipairs( classes ) do
    local owned
    for _, weapon in ipairs( ents.FindByClass( spec[ 1 ] ) ) do
        if IsValid( weapon ) and weapon:GetOwner() == player then owned = weapon end
    end
    print( spec[ 1 ],
        "owned=" .. tostring( IsValid( owned ) ),
        "clip=" .. ( IsValid( owned ) and tostring( owned:Clip1() ) or "n/a" ),
        spec[ 2 ] .. "=" .. tostring( player:GetAmmoCount( spec[ 2 ] ) ),
        spec[ 3 ] and ( spec[ 3 ] .. "=" .. tostring( player:GetAmmoCount( spec[ 3 ] ) ) ) or "" )
end
