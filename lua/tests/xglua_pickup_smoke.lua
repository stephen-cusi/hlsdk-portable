--[[---------------------------------------------------------------------------
	xglua_pickup_smoke.lua -- PlayerCanPickupWeapon / PlayerCanPickupItem smoke

	Load with the server console command (NOT autorun):
	    lua_openscript tests/xglua_pickup_smoke.lua
	or via exec cfg/xglua_pickup_smoke.cfg. Turn it off again with
	exec cfg/xglua_pickup_smoke_off.cfg - the hooks then return nil (the
	native allow path) until the script is loaded again.

	Safe to load repeatedly: re-loading replaces the same hook ids and
	rebuilds the target lists. Only the classnames listed below are denied
	(a strict boolean false); every other entity returns nil and keeps the
	native behavior. Nothing here mutates entities or players.
-----------------------------------------------------------------------------]]

XGLuaPickupSmoke = XGLuaPickupSmoke or {}

local Smoke = XGLuaPickupSmoke

--定向拒绝目标(按 classname 精确匹配;按需增删)
--Defaults install only when absent, so a reload keeps lists edited through
--XGLuaPickupSmoke.DeniedWeapons / DeniedItems (set them to nil to restore
--the defaults on the next load; a fresh map VM starts from defaults anyway).
Smoke.DeniedWeapons = Smoke.DeniedWeapons or {
	"weapon_crowbar",
	"weapon_9mmhandgun",
}

Smoke.DeniedItems = Smoke.DeniedItems or {
	"item_healthkit",
	"item_battery",
}

local function denyLookup( list )
	local set = {}
	for i = 1, #list do set[ list[i] ] = true end
	return set
end

local deniedWeapons = denyLookup( Smoke.DeniedWeapons )
local deniedItems = denyLookup( Smoke.DeniedItems )
local loggedWeapons = {}
local loggedItems = {}

function Smoke.Enable()
	Smoke.Active = true
	loggedWeapons = {}
	loggedItems = {}
	print( "[XGLua] pickup smoke: enabled (weapons: " .. table.concat( Smoke.DeniedWeapons, ", " )
		.. " | items: " .. table.concat( Smoke.DeniedItems, ", " ) .. ")" )
end

function Smoke.Disable()
	Smoke.Active = false
	print( "[XGLua] pickup smoke: disabled - hooks return nil (native allow path)" )
end

hook.Add( "PlayerCanPickupWeapon", "xglua_pickup_smoke", function( ply, wep )
	if not Smoke.Active then return nil end
	if not IsValid( wep ) then return nil end
	local class = wep:GetClass()
	if deniedWeapons[ class ] then
		local index = tostring( wep:EntIndex() )
		if not loggedWeapons[ index ] then
			loggedWeapons[ index ] = true
			print( "[XGLua] pickup smoke: denying weapon '" .. class .. "'" )
		end
		return false
	end
	return nil
end )

hook.Add( "PlayerCanPickupItem", "xglua_pickup_smoke", function( ply, item )
	if not Smoke.Active then return nil end
	if not IsValid( item ) then return nil end
	local class = item:GetClass()
	if deniedItems[ class ] then
		local index = tostring( item:EntIndex() )
		if not loggedItems[ index ] then
			loggedItems[ index ] = true
			print( "[XGLua] pickup smoke: denying item '" .. class .. "'" )
		end
		return false
	end
	return nil
end )

print( "[XGLua] pickup smoke: hooks registered (id 'xglua_pickup_smoke')" )
Smoke.Enable()
