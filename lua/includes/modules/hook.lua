--[[---------------------------------------------------------------------------
	hook.lua -- XGLua hook library (GMod-compatible)
	Event/identifier based callback registry. Semantics follow GMod:
	any non-nil return from a hook stops the chain and is returned by
	hook.Call; otherwise the gamemode function GAMEMODE[event] is called.
	Additionally every callback is xpcall-protected and a hook that errors
	10 times in a row is removed automatically.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_HOOK ) then return end
XGLUA_MODULE_HOOK = true

hook = hook or {}

local Hooks = {}        -- Hooks[event][identifier] = function
local ErrorCount = {}   -- ErrorCount[event][identifier] = consecutive error count
local MAX_ERRORS = 10

local pairs = pairs
local tostring = tostring
local select = select
local unpack = unpack

-- Preserve nils, including trailing nils, in callback return values.
local function Pack( ... )
	return { n = select( "#", ... ), ... }
end

--[[---------------------------------------------------------------------------
	Name: hook.GetTable()
	Desc: Returns the internal two-level hook table (event -> identifier -> fn)
-----------------------------------------------------------------------------]]
function hook.GetTable()
	return Hooks
end

--[[---------------------------------------------------------------------------
	Name: hook.Add( event, identifier, fn )
	Desc: Registers a callback for the given event. Identifier can be any
		non-nil value (usually a string).
-----------------------------------------------------------------------------]]
function hook.Add( event, identifier, fn )
	if ( event == nil ) then
		print( "[XGLua] hook.Add: bad argument #1 (event expected, got nil)" )
		return
	end
	if ( identifier == nil ) then
		print( "[XGLua] hook.Add: bad argument #2 (identifier expected, got nil)" )
		return
	end
	if ( type( fn ) ~= "function" ) then
		print( "[XGLua] hook.Add: bad argument #3 (function expected) - event '" .. tostring( event ) .. "'" )
		return
	end

	local tbl = Hooks[ event ]
	if ( tbl == nil ) then
		tbl = {}
		Hooks[ event ] = tbl
	end

	tbl[ identifier ] = fn
	if ( ErrorCount[ event ] ~= nil ) then ErrorCount[ event ][ identifier ] = nil end
end

--[[---------------------------------------------------------------------------
	Name: hook.Remove( event, identifier )
	Desc: Removes the callback registered under the given event/identifier.
-----------------------------------------------------------------------------]]
function hook.Remove( event, identifier )
	local tbl = Hooks[ event ]
	if ( tbl == nil ) then return end

	if ( identifier == nil ) then return end
	tbl[ identifier ] = nil
	if ( ErrorCount[ event ] ~= nil ) then ErrorCount[ event ][ identifier ] = nil end
end

-- Error handler for xpcall: decorates the error message with a traceback.
local function ErrorHandler( err )
	return debug.traceback( tostring( err ), 2 )
end

--[[---------------------------------------------------------------------------
	Name: hook.Call( event, gm, ... )
	Desc: Calls all hooks registered for the event in undefined (pairs)
		order. The first non-nil return value terminates the chain and is
		returned (all return values are forwarded). If nothing returned a
		value, falls back to the gamemode function GAMEMODE[event] (gm is
		the gamemode table, called with gm as the first argument). The
		callback list is snapshotted before iteration so callbacks may
		safely add/remove hooks mid-call.
-----------------------------------------------------------------------------]]
function hook.Call( event, gm, ... )
	local HookTable = Hooks[ event ]
	if ( HookTable ~= nil ) then

		-- Keep ids and functions in separate entry tables: identifiers may
		-- themselves be integers and must never alias array positions.
		local snapshot = {}
		for id, fn in pairs( HookTable ) do
			snapshot[ #snapshot + 1 ] = { id, fn }
		end

		for i = 1, #snapshot do
			local id, fn = snapshot[ i ][ 1 ], snapshot[ i ][ 2 ]
			-- xpcall forwards arguments and results in LuaJIT. The result is
			-- local to this invocation so recursive hooks cannot overwrite it.
			local result = Pack( xpcall( fn, ErrorHandler, ... ) )
			local current = Hooks[ event ] and Hooks[ event ][ id ] == fn
			if ( not result[ 1 ] ) then
				print( "[XGLua] hook error in " .. tostring( event ) .. "/" .. tostring( id ) .. ": " .. tostring( result[ 2 ] ) )
				if ( current ) then
					local counts = ErrorCount[ event ]
					if ( counts == nil ) then counts = {}; ErrorCount[ event ] = counts end
					local errors = ( counts[ id ] or 0 ) + 1
					counts[ id ] = errors
					if ( errors >= MAX_ERRORS ) then
						print( "[XGLua] hook '" .. tostring( event ) .. "/" .. tostring( id ) .. "' errored " .. errors .. " times in a row, removing it" )
						Hooks[ event ][ id ] = nil
						counts[ id ] = nil
					end
				end
			else
				if ( current and ErrorCount[ event ] ~= nil ) then ErrorCount[ event ][ id ] = nil end
				if ( result[ 2 ] ~= nil ) then
					return unpack( result, 2, result.n )
				end
			end
		end
	end

	-- No hook returned anything: fall back to the gamemode function
	if ( gm == nil ) then return end

	local GamemodeFunction = gm[ event ]
	if ( GamemodeFunction == nil ) then return end

	return GamemodeFunction( gm, ... )
end

--[[---------------------------------------------------------------------------
	Name: hook.Run( event, ... )
	Desc: Convenience wrapper: hook.Call( event, GAMEMODE, ... )
-----------------------------------------------------------------------------]]
function hook.Run( event, ... )
	return hook.Call( event, GAMEMODE, ... )
end
