--[[---------------------------------------------------------------------------
	init.lua -- XGLua core library bootstrap
	Executed by hl.dll right after the Lua VM is created. Loads the standard
	GMod-style library layer in a fixed order, installs a fallback print()
	(the C++ side overrides it with the real engine console implementation)
	and exposes the META_ENT placeholder used by the entity binding layer.
-----------------------------------------------------------------------------]]

if ( XGLUA_INIT_LOADED ) then return end
XGLUA_INIT_LOADED = true

-- Fallback print(): replaced by the C++ binding with the real console
-- implementation, but keeps scripts usable in plain LuaJIT testing.
if ( print == nil ) then
	print = function( ... )
		local n = select( "#", ... )
		local parts = {}
		for i = 1, n do
			parts[ i ] = tostring( select( i, ... ) )
		end
		io.write( table.concat( parts, "\t" ), "\n" )
	end
end

-- Modules (GMod-style global tables, loaded in dependency order)
include( "includes/modules/hook.lua" )
include( "includes/modules/timer.lua" )
include( "includes/modules/weapons.lua" )
include( "includes/modules/gamemode.lua" )

-- Extensions (add functions onto the standard libraries)
include( "includes/extensions/string.lua" )
include( "includes/extensions/table.lua" )
include( "includes/extensions/math.lua" )

-- HL1 engine constants
include( "includes/enum.lua" )

-- Placeholder for the entity metatable registry. The C++ binding layer
-- installs the real userdata metatable here (Entities/Object metatables
-- register themselves against it); scripts can pre-index shared helpers.
META_ENT = META_ENT or {}
