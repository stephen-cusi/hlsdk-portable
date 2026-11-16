--[[---------------------------------------------------------------------------
	math.lua -- XGLua math library extensions (GMod-compatible)
	Adds the GMod math.* helpers onto the standard Lua math library:
	Clamp, Round, Approach, Remap, Rand, NormalizeAngle, AngleDifference,
	EaseInOut and a few small extras. Ported from the official GMod
	implementation (Facepunch/garrysmod).
-----------------------------------------------------------------------------]]

if ( XGLUA_EXT_MATH ) then return end
XGLUA_EXT_MATH = true

math.tau = math.pi * 2

--[[---------------------------------------------------------------------------
	Name: math.Clamp( value, low, high )
	Desc: Clamps value between low and high.
-----------------------------------------------------------------------------]]
function math.Clamp( _in, low, high )
	return math.min( math.max( _in, low ), high )
end

--[[---------------------------------------------------------------------------
	Name: math.Round( value, idp )
	Desc: Rounds to the nearest integer (or to idp decimal places).
-----------------------------------------------------------------------------]]
function math.Round( num, idp )
	local mult = 10 ^ ( idp or 0 )
	return math.floor( num * mult + 0.5 ) / mult
end

--[[---------------------------------------------------------------------------
	Name: math.Truncate( value, idp )  (GMod parity bonus)
	Desc: Rounds towards zero.
-----------------------------------------------------------------------------]]
function math.Truncate( num, idp )
	local mult = 10 ^ ( idp or 0 )
	return ( num < 0 and math.ceil or math.floor )( num * mult ) / mult
end

--[[---------------------------------------------------------------------------
	Name: math.Rand( low, high )
	Desc: Random float between low and high.
-----------------------------------------------------------------------------]]
function math.Rand( low, high )
	return low + ( high - low ) * math.random()
end

--[[---------------------------------------------------------------------------
	Name: math.Approach( current, target, increment )
	Desc: Moves current towards target by at most |increment|, never
		overshooting. Useful for per-frame smoothing.
-----------------------------------------------------------------------------]]
function math.Approach( cur, target, inc )
	if ( cur < target ) then
		return math.min( cur + math.abs( inc ), target )
	end

	if ( cur > target ) then
		return math.max( cur - math.abs( inc ), target )
	end

	return target
end

--[[---------------------------------------------------------------------------
	Name: math.NormalizeAngle( a )
	Desc: Normalizes an angle into the range -180..180.
-----------------------------------------------------------------------------]]
function math.NormalizeAngle( a )
	return ( a + 180 ) % 360 - 180
end

--[[---------------------------------------------------------------------------
	Name: math.AngleDifference( a, b )
	Desc: Shortest signed difference between two angles (-180..180).
-----------------------------------------------------------------------------]]
function math.AngleDifference( a, b )
	local diff = math.NormalizeAngle( a - b )

	if ( diff < 180 ) then
		return diff
	end

	return diff - 360
end

--[[---------------------------------------------------------------------------
	Name: math.ApproachAngle( current, target, increment )  (GMod parity bonus)
	Desc: math.Approach for angles, taking the wrap-around into account.
-----------------------------------------------------------------------------]]
function math.ApproachAngle( cur, target, inc )
	return math.Approach( cur, cur + math.AngleDifference( target, cur ), inc )
end

--[[---------------------------------------------------------------------------
	Name: math.Remap( value, inMin, inMax, outMin, outMax )
	Desc: Maps value from the [inMin, inMax] range onto [outMin, outMax].
-----------------------------------------------------------------------------]]
function math.Remap( value, inMin, inMax, outMin, outMax )
	return outMin + ( ( ( value - inMin ) / ( inMax - inMin ) ) * ( outMax - outMin ) )
end

--[[---------------------------------------------------------------------------
	Name: math.EaseInOut( fraction, easeIn, easeOut )
	Desc: Ease in/out curve. All parameters 0-1. easeIn/easeOut control how
		much of the curve is spent accelerating/decelerating (defaults 0/1,
		i.e. pure ease-out, matching GMod).
-----------------------------------------------------------------------------]]
function math.EaseInOut( frac, easeIn, easeOut )
	if ( frac == 0 or frac == 1 ) then return frac end

	if ( easeIn == nil ) then easeIn = 0 end
	if ( easeOut == nil ) then easeOut = 1 end

	local fSumEase = easeIn + easeOut

	if ( fSumEase == 0 ) then return frac end
	if ( fSumEase > 1 ) then
		easeIn = easeIn / fSumEase
		easeOut = easeOut / fSumEase
	end

	local fProgressCalc = 1 / ( 2 - easeIn - easeOut )

	if ( frac < easeIn ) then
		return ( ( fProgressCalc / easeIn ) * frac * frac )
	elseif ( frac < 1 - easeOut ) then
		return ( fProgressCalc * ( 2 * frac - easeIn ) )
	else
		frac = 1 - frac
		return ( 1 - ( fProgressCalc / easeOut ) * frac * frac )
	end
end

--[[---------------------------------------------------------------------------
	Name: math.Sign( num )  (GMod parity bonus)
	Desc: -1 for negative, 1 for positive, 0 for zero.
-----------------------------------------------------------------------------]]
function math.Sign( num )
	return ( num > 0 and 1 ) or ( num < 0 and -1 ) or 0
end

--[[---------------------------------------------------------------------------
	Name: math.TimeFraction( start, end, current )  (GMod parity bonus)
	Desc: 0-1 progress of current between start and end.
-----------------------------------------------------------------------------]]
function math.TimeFraction( start, _end, current )
	return ( current - start ) / ( _end - start )
end

--[[---------------------------------------------------------------------------
	Name: math.IsNearlyEqual( a, b, tolerance )  (GMod parity bonus)
	Desc: Floating point comparison with a tolerance (default 1e-8).
-----------------------------------------------------------------------------]]
function math.IsNearlyEqual( a, b, tolerance )
	if ( tolerance == nil ) then
		tolerance = 1e-8
	end

	return math.abs( a - b ) <= tolerance
end
