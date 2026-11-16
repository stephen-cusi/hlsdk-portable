--[[---------------------------------------------------------------------------
	timer.lua -- XGLua timer library (GMod-compatible)
	Named timers driven by the engine clock. C++ calls timer.Tick() once
	per game frame; all timing is based on the global CurTime() supplied
	by the binding layer, so timers pause with the server.

	repetitions = 0 means the timer repeats forever. Callers may pass a
	vararg of extra parameters to the callback (GMod parity).
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_TIMER ) then return end
XGLUA_MODULE_TIMER = true

timer = timer or {}

local Timers = {}           -- Timers[name] = timer table
local SimpleId = 0          -- auto-incrementing ids for timer.Simple

local tostring = tostring
local select = select
local unpack = unpack

-- Error handler for xpcall, mirrors hook.lua output style
local function ErrorHandler( err )
	return debug.traceback( tostring( err ), 2 )
end

-- Extra args to forward to the currently firing callback
local argbuf = {}
local nargs = 0

local function Runner( fn )
	if ( nargs == 0 ) then
		return fn()
	end
	return fn( unpack( argbuf, 1, nargs ) )
end

--[[---------------------------------------------------------------------------
	Name: timer.Create( name, delay, repetitions, fn, ... )
	Desc: Creates (or replaces) a named timer. delay is in seconds,
		repetitions 0 = repeat forever. Extra varargs are passed to fn.
-----------------------------------------------------------------------------]]
function timer.Create( name, delay, repetitions, fn, ... )
	if ( delay == nil or delay < 0 ) then
		print( "[XGLua] timer.Create: invalid delay for timer '" .. tostring( name ) .. "'" )
		return
	end
	if ( fn == nil ) then
		print( "[XGLua] timer.Create: function expected for timer '" .. tostring( name ) .. "', got nil" )
		return
	end

	-- Store the extra callback arguments
	local extra = {}
	local count = select( "#", ... )
	for i = 1, count do
		extra[ i ] = select( i, ... )
	end

	Timers[ name ] = {
		Delay        = delay,
		Repetitions  = repetitions or 0,
		Fn           = fn,
		ExtraArgs    = extra,
		NextTick     = CurTime() + delay,
		Paused       = false,
	}
end

--[[---------------------------------------------------------------------------
	Name: timer.Remove( name )
	Desc: Removes a timer entirely. Does nothing for unknown names.
-----------------------------------------------------------------------------]]
function timer.Remove( name )
	local t = Timers[ name ]
	if ( t == nil ) then return end

	Timers[ name ] = nil
end

--[[---------------------------------------------------------------------------
	Name: timer.Exists( name )
	Desc: Returns true if a timer with this name exists (paused counts as
		existing), false otherwise.
-----------------------------------------------------------------------------]]
function timer.Exists( name )
	return Timers[ name ] ~= nil
end

--[[---------------------------------------------------------------------------
	Name: timer.Simple( delay, fn, ... )
	Desc: Creates a one-shot timer with an automatically generated name.
		Extra varargs are passed to fn.
-----------------------------------------------------------------------------]]
function timer.Simple( delay, fn, ... )
	SimpleId = SimpleId + 1
	timer.Create( "Timer" .. tostring( SimpleId ), delay, 1, fn, ... )
end

--[[---------------------------------------------------------------------------
	Name: timer.Adjust( name, delay, repetitions, fn, ... )
	Desc: Changes a timer's parameters. Arguments passed as nil keep their
		current value. The elapsed fraction of the current interval is
		preserved proportionally when delay changes.
-----------------------------------------------------------------------------]]
function timer.Adjust( name, delay, repetitions, fn, ... )
	local t = Timers[ name ]
	if ( t == nil ) then
		print( "[XGLua] timer.Adjust: timer '" .. tostring( name ) .. "' not found" )
		return
	end

	if ( delay ~= nil ) then
		local now = CurTime()
		-- Preserve the remaining fraction of the current interval
		local timeleft = t.NextTick - now
		if ( timeleft < 0 ) then timeleft = 0 end
		local frac = ( t.Delay > 0 ) and ( timeleft / t.Delay ) or 0
		t.Delay = delay
		t.NextTick = now + frac * delay
	end

	if ( repetitions ~= nil ) then t.Repetitions = repetitions end
	if ( fn ~= nil ) then t.Fn = fn end

	local count = select( "#", ... )
	if ( count > 0 ) then
		local extra = {}
		for i = 1, count do
			extra[ i ] = select( i, ... )
		end
		t.ExtraArgs = extra
	end
end

--[[---------------------------------------------------------------------------
	Name: timer.Pause( name ) / timer.UnPause( name ) / timer.Toggle( name )
	Desc: Pause/resume a timer without losing its remaining time.
-----------------------------------------------------------------------------]]
function timer.Pause( name )
	local t = Timers[ name ]
	if ( t == nil ) then return false end

	t.Paused = true
	return true
end

function timer.UnPause( name )
	local t = Timers[ name ]
	if ( t == nil ) then return false end

	t.Paused = false
	return true
end

function timer.Toggle( name )
	local t = Timers[ name ]
	if ( t == nil ) then return false end

	t.Paused = not t.Paused
	return not t.Paused
end

--[[---------------------------------------------------------------------------
	Name: timer.Stop( name )
	Desc: Stops a timer (pauses it). Use timer.Start() to restart it.
-----------------------------------------------------------------------------]]
function timer.Stop( name )
	local t = Timers[ name ]
	if ( t == nil ) then return false end

	t.Paused = true
	return true
end

--[[---------------------------------------------------------------------------
	Name: timer.Start( name )
	Desc: Restarts a stopped/paused timer from scratch (full delay).
-----------------------------------------------------------------------------]]
function timer.Start( name )
	local t = Timers[ name ]
	if ( t == nil ) then
		print( "[XGLua] timer.Start: timer '" .. tostring( name ) .. "' not found" )
		return false
	end

	t.Paused = false
	t.NextTick = CurTime() + t.Delay
	return true
end

--[[---------------------------------------------------------------------------
	Name: timer.TimeLeft( name )
	Desc: Seconds until the timer's next tick (0 for unknown timers).
-----------------------------------------------------------------------------]]
function timer.TimeLeft( name )
	local t = Timers[ name ]
	if ( t == nil or t.Paused ) then return 0 end

	local left = t.NextTick - CurTime()
	if ( left < 0 ) then left = 0 end
	return left
end

--[[---------------------------------------------------------------------------
	Name: timer.RepsLeft( name )
	Desc: Number of repetitions left (-1 = infinite, nil = no timer).
-----------------------------------------------------------------------------]]
function timer.RepsLeft( name )
	local t = Timers[ name ]
	if ( t == nil ) then return nil end
	if ( t.Repetitions == 0 ) then return -1 end

	return t.Repetitions
end

--[[---------------------------------------------------------------------------
	Name: timer.Tick()
	Desc: Called by C++ once per game frame. Fires every timer whose next
		tick time has passed. Callbacks run inside xpcall; a callback
		error prints and does not remove the timer (it keeps ticking).
-----------------------------------------------------------------------------]]
function timer.Tick()
	local now = CurTime()

	-- Snapshot timer names: callbacks may create/remove timers (e.g.
	-- timer.Simple called from inside a timer), which would otherwise
	-- corrupt the pairs() iteration over the live table.
	local names = {}
	local count = 0
	for name in pairs( Timers ) do
		count = count + 1
		names[ count ] = name
	end

	for i = 1, count do
		local name = names[ i ]
		local t = Timers[ name ]
		if ( t ~= nil and not t.Paused and t.NextTick <= now ) then
			-- Schedule the next tick BEFORE running the callback, so the
			-- callback may safely remove/recreate/adjust this timer.
			if ( t.Repetitions > 0 ) then
				t.Repetitions = t.Repetitions - 1
				if ( t.Repetitions > 0 ) then
					t.NextTick = now + t.Delay
				else
					-- Repetitions exhausted: this is the final firing
					Timers[ name ] = nil
				end
			else
				-- Repetitions == 0: repeats forever
				t.NextTick = now + t.Delay
			end

			-- Forward extra args
			local extra = t.ExtraArgs
			nargs = ( extra ~= nil ) and #extra or 0
			for j = 1, nargs do
				argbuf[ j ] = extra[ j ]
			end

			local ok, err = xpcall( Runner, ErrorHandler, t.Fn )
			if ( not ok ) then
				print( "[XGLua] timer error in '" .. tostring( name ) .. "': " .. tostring( err ) )
			end

			-- Clear the scratch buffer so we don't hold references
			for j = 1, nargs do
				argbuf[ j ] = nil
			end
			nargs = 0
		end
	end
end

--[[---------------------------------------------------------------------------
	Name: timer.GetTimers()  (XGLua extension)
	Desc: Returns the internal timer table (name -> timer data).
-----------------------------------------------------------------------------]]
function timer.GetTimers()
	return Timers
end
