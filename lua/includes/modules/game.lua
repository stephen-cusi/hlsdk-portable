--[[---------------------------------------------------------------------------
	game.lua -- GMod-compatible game & custom ammo type registry
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_GAME ) then return end
XGLUA_MODULE_GAME = true

game = game or {}

local RegisteredAmmo = {}

function game.AddAmmoType( ammoData )
	if ( type( ammoData ) ~= "table" ) then
		error( "game.AddAmmoType: expected table, got " .. type( ammoData ) )
	end

	local name = ammoData.name
	if ( type( name ) ~= "string" or name == "" ) then
		error( "game.AddAmmoType: ammoData.name must be a non-empty string" )
	end

	local maxCarry = math.floor( tonumber( ammoData.maxcarry ) or 68 )
	local key = string.lower( name )

	local entry = {
		name     = name,
		maxcarry = maxCarry,
		dmgtype  = ammoData.dmgtype or 0,
		tracer   = ammoData.tracer or 0
	}

	if ( SERVER and type( XGLua_RegisterAmmoType ) == "function" ) then
		entry.id = XGLua_RegisterAmmoType( name, maxCarry )
	end

	RegisteredAmmo[ key ] = entry
	return entry.id or -1
end

function game.GetAmmoID( name )
	if ( type( name ) ~= "string" ) then return -1 end
	local entry = RegisteredAmmo[ string.lower( name ) ]
	return entry and ( entry.id or -1 ) or -1
end

function game.GetAmmoMax( name )
	if ( type( name ) ~= "string" ) then return -1 end
	local entry = RegisteredAmmo[ string.lower( name ) ]
	return entry and entry.maxcarry or -1
end

function game.GetAmmoTypes()
	return RegisteredAmmo
end
