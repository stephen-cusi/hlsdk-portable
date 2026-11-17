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

--[[---------------------------------------------------------------------------
	Name: DeriveGamemode( name )
	Desc: Global, GMod-style. Declares that the gamemode being loaded (the
		current GM table) derives from the registered gamemode <name> by
		setting GM.BaseClass. The base must already be registered - the C++
		loader resolves the txt "base" chain and runs it first, so a base
		named in metadata is always available by the time derived scripts
		run. This function never reads files or re-runs base scripts.

		Calling it outside gamemode script loading (GM not a table yet) or
		with an unregistered name raises a Lua error naming the mode.
		Repeated calls with the same base are safe and idempotent.
-----------------------------------------------------------------------------]]
function DeriveGamemode( name )
	local nameType = type( name )
	if ( nameType ~= "string" ) then
		local shown = nameType
		if ( nameType == "number" or nameType == "boolean" ) then
			shown = tostring( name )
		elseif ( nameType ~= "nil" ) then
			shown = "<" .. nameType .. ">"
		end
		error( string.format( "DeriveGamemode(%s): bad argument #1 (string expected, got %s)",
			shown, nameType ), 2 )
	end

	if ( type( GM ) ~= "table" ) then
		error( string.format( "DeriveGamemode('%s'): GM is not a table (called outside gamemode loading; GM is %s)",
			name, type( GM ) ), 2 )
	end

	local base = gamemode.Get( name )
	if ( base == nil ) then
		error( string.format( "DeriveGamemode('%s'): base gamemode is not registered", name ), 2 )
	end

	-- rawequal skips any __eq metamethod and makes the same-table repeat
	-- call an explicit no-op; a different registered base overrides.
	if ( rawequal( GM.BaseClass, base ) ) then return end
	GM.BaseClass = base
end
