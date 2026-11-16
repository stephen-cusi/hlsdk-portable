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
local ErrorCount = {}   -- ErrorCount[event.."\1"..tostring(id)] = consecutive error count
local MAX_ERRORS = 10

local pairs = pairs
local tostring = tostring
local select = select
local unpack = unpack

-- Scratch buffers shared across hook.Call invocations (avoids per-call
-- allocations on the hot path; cleared before returning).
local argbuf = {}
local nargs = 0
local retvals = {}
local retcount = 0

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
	if ( fn == nil ) then
		print( "[XGLua] hook.Add: bad argument #3 (function expected, got nil) - event '" .. tostring( event ) .. "'" )
		return
	end

	local tbl = Hooks[ event ]
	if ( tbl == nil ) then
		tbl = {}
		Hooks[ event ] = tbl
	end

	tbl[ identifier ] = fn
	ErrorCount[ tostring( event ) .. "\1" .. tostring( identifier ) ] = nil
end

--[[---------------------------------------------------------------------------
	Name: hook.Remove( event, identifier )
	Desc: Removes the callback registered under the given event/identifier.
-----------------------------------------------------------------------------]]
function hook.Remove( event, identifier )
	local tbl = Hooks[ event ]
	if ( tbl == nil ) then return end

	tbl[ identifier ] = nil
	ErrorCount[ tostring( event ) .. "\1" .. tostring( identifier ) ] = nil
end

-- Error handler for xpcall: decorates the error message with a traceback.
local function ErrorHandler( err )
	return debug.traceback( tostring( err ), 2 )
end

-- Runs the hook function against the shared argbuf snapshot and captures
-- ALL of its return values into retvals (xpcall alone only forwards one).
local function Runner( fn )
	local n = nargs
	if ( n == 0 ) then
		retvals = { fn() }
	elseif ( n == 1 ) then
		retvals = { fn( argbuf[ 1 ] ) }
	elseif ( n == 2 ) then
		retvals = { fn( argbuf[ 1 ], argbuf[ 2 ] ) }
	elseif ( n == 3 ) then
		retvals = { fn( argbuf[ 1 ], argbuf[ 2 ], argbuf[ 3 ] ) }
	elseif ( n == 4 ) then
		retvals = { fn( argbuf[ 1 ], argbuf[ 2 ], argbuf[ 3 ], argbuf[ 4 ] ) }
	else
		retvals = { fn( unpack( argbuf, 1, n ) ) }
	end
	retcount = #retvals
end

local function ClearArgs()
	for i = 1, nargs do
		argbuf[ i ] = nil
	end
	nargs = 0
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

		-- Snapshot: calling next() over the live table breaks if callbacks
		-- add/remove entries mid-iteration.
		local snapshot = {}
		local n = 0
		for k, v in pairs( HookTable ) do
			n = n + 1
			snapshot[ n ] = k
			snapshot[ k ] = v
		end

		-- Copy varargs into argbuf once per hook.Call (not per hook)
		local count = select( "#", ... )
		for i = 1, count do
			argbuf[ i ] = select( i, ... )
		end
		nargs = count

		for i = 1, n do
			local id = snapshot[ i ]
			local fn = snapshot[ id ]
			if ( fn ~= nil ) then
				local ok, err = xpcall( Runner, ErrorHandler, fn )

				local key = tostring( event ) .. "\1" .. tostring( id )
				if ( not ok ) then
					print( "[XGLua] hook error in " .. tostring( event ) .. "/" .. tostring( id ) .. ": " .. tostring( err ) )

					local errors = ( ErrorCount[ key ] or 0 ) + 1
					ErrorCount[ key ] = errors

					if ( errors >= MAX_ERRORS ) then
						print( "[XGLua] hook '" .. tostring( event ) .. "/" .. tostring( id ) .. "' errored " .. errors .. " times in a row, removing it" )
						if ( Hooks[ event ] ~= nil ) then
							Hooks[ event ][ id ] = nil
						end
						ErrorCount[ key ] = nil
					end
				else
					ErrorCount[ key ] = nil

					-- Hook returned a value -> overrides the gamemode function
					if ( retvals[ 1 ] ~= nil ) then
						local vals = retvals
						local num = retcount
						ClearArgs()
						return unpack( vals, 1, num )
					end
				end
			end
		end

		ClearArgs()
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
