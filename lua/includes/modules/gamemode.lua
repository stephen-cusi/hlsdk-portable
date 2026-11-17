--[[---------------------------------------------------------------------------
	gamemode.lua -- XGLua gamemode library (GMod-compatible subset)
	The C++ loader (dlls/lua/xglua_gamemode.h) resolves the base chain from
	gamemodes/<name>/<name>.txt, runs shared.lua then init.lua per mode with
	a fresh GM table, and registers each finished table here. The selected
	mode's table is installed as the global GAMEMODE, which hook.Run uses as
	its gamemode fallback.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_GAMEMODE ) then return end
XGLUA_MODULE_GAMEMODE = true

gamemode = gamemode or {}

local Stored = {}	-- Stored[lowername] = GM table

--[[---------------------------------------------------------------------------
	Name: gamemode.Register( gm, name )
	Desc: Registers a gamemode table under the given name (case-insensitive,
		normalized to lower case like weapons.Register). Re-registering
		replaces the previous table.
-----------------------------------------------------------------------------]]
function gamemode.Register( gm, name )
	if ( type( gm ) ~= "table" ) then
		print( "[XGLua] gamemode.Register: bad argument #1 (table expected, got " .. type( gm ) .. ")" )
		return
	end
	if ( name == nil ) then
		print( "[XGLua] gamemode.Register: bad argument #2 (string name expected, got nil)" )
		return
	end

	name = string.lower( tostring( name ) )
	Stored[ name ] = gm
	return gm
end

--[[---------------------------------------------------------------------------
	Name: gamemode.Get( name )
	Desc: Returns the registered gamemode table for the name (nil if not
		registered).
-----------------------------------------------------------------------------]]
function gamemode.Get( name )
	if ( name == nil ) then return nil end
	return Stored[ string.lower( tostring( name ) ) ]
end

--[[---------------------------------------------------------------------------
	Name: gamemode.GetAll()
	Desc: Returns a copy of the name -> table mapping of every registered
		gamemode.
-----------------------------------------------------------------------------]]
function gamemode.GetAll()
	local out = {}
	for name, gm in pairs( Stored ) do
		out[ name ] = gm
	end
	return out
end
