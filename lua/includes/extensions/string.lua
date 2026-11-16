--[[---------------------------------------------------------------------------
	string.lua -- XGLua string library extensions (GMod-compatible)
	Adds the GMod string.* helpers onto the standard Lua string library:
	Explode, Split, Trim family, Replace, FormattedTime, StartWith/EndsWith.
	Ported from the official GMod implementation (Facepunch/garrysmod).
-----------------------------------------------------------------------------]]

if ( XGLUA_EXT_STRING ) then return end
XGLUA_EXT_STRING = true

local string_sub = string.sub
local string_gsub = string.gsub
local string_len = string.len
local string_find = string.find
local string_format = string.format
local string_match = string.match

--[[---------------------------------------------------------------------------
	Name: string.ToTable( str )
	Desc: Converts a string to a table of single characters.
-----------------------------------------------------------------------------]]
function string.ToTable( str )
	local tbl = {}
	str = tostring( str )
	for i = 1, #str do
		tbl[ i ] = string_sub( str, i, i )
	end
	return tbl
end

--[[---------------------------------------------------------------------------
	Name: string.PatternSafe( str )
	Desc: Escapes a string so it can be used as a literal inside a Lua pattern.
-----------------------------------------------------------------------------]]
local pattern_escape_replacements = {
	["("] = "%(",
	[")"] = "%)",
	["."] = "%.",
	["%"] = "%%",
	["+"] = "%+",
	["-"] = "%-",
	["*"] = "%*",
	["?"] = "%?",
	["["] = "%[",
	["]"] = "%]",
	["^"] = "%^",
	["$"] = "%$",
	["\0"] = "%z"
}

function string.PatternSafe( str )
	return ( string_gsub( str, ".", pattern_escape_replacements ) )
end

--[[---------------------------------------------------------------------------
	Name: string.Explode( separator, str, withpattern )
	Desc: Splits a string into a table around the separator. By default the
		separator is treated as a plain string; pass withpattern=true to
		treat it as a Lua pattern. Empty separator splits into characters.
-----------------------------------------------------------------------------]]
function string.Explode( separator, str, withpattern )
	if ( separator == "" ) then return string.ToTable( str ) end
	if ( withpattern == nil ) then withpattern = false end

	local ret = {}
	local current_pos = 1
	local i = 0

	for _ = 1, string_len( str ) do
		local start_pos, end_pos = string_find( str, separator, current_pos, not withpattern )
		if ( start_pos == nil ) then break end

		i = i + 1
		ret[ i ] = string_sub( str, current_pos, start_pos - 1 )
		current_pos = end_pos + 1
	end

	ret[ i + 1 ] = string_sub( str, current_pos )

	return ret
end

--[[---------------------------------------------------------------------------
	Name: string.Split( str, delimiter )
	Desc: GMod argument order (string first!) for string.Explode.
-----------------------------------------------------------------------------]]
function string.Split( str, delimiter )
	return string.Explode( delimiter, str )
end

--[[---------------------------------------------------------------------------
	Name: string.Replace( str, what, with )
	Desc: Replaces all occurrences of 'what' with 'with'.
-----------------------------------------------------------------------------]]
function string.Replace( str, what, with )
	local tbl = string.Split( str, what )
	if ( tbl[ 1 ] ) then return table.concat( tbl, with ) end
	return str
end

--[[---------------------------------------------------------------------------
	Name: string.Trim( s, char ) / TrimLeft / TrimRight
	Desc: Removes leading/trailing whitespace (or the given character(s))
		from a string. char is pattern-safe when provided.
-----------------------------------------------------------------------------]]
function string.Trim( s, char )
	if ( char ) then char = string.PatternSafe( char ) else char = "%s" end
	return string_match( s, "^" .. char .. "*(.-)" .. char .. "*$" ) or s
end

function string.TrimRight( s, char )
	if ( char ) then char = string.PatternSafe( char ) else char = "%s" end
	return string_match( s, "^(.-)" .. char .. "*$" ) or s
end

function string.TrimLeft( s, char )
	if ( char ) then char = string.PatternSafe( char ) else char = "%s" end
	return string_match( s, "^" .. char .. "*(.-)$" ) or s
end

--[[---------------------------------------------------------------------------
	Name: string.FormattedTime( seconds, format )
	Desc: Given a time in seconds returns { h, m, s, ms } as a table, or a
		formatted string when a format string is given. Tokens h/m/s/ms map
		to minutes/seconds/milliseconds, e.g. string.FormattedTime( 123.456,
		"%02i:%02i:%02i" ) -> "02:03:45". ms is floored to a whole number
		so %i tokens never error on fractional values.
-----------------------------------------------------------------------------]]
function string.FormattedTime( seconds, format )
	if ( seconds == nil ) then seconds = 0 end

	local hours = math.floor( seconds / 3600 )
	local minutes = math.floor( ( seconds / 60 ) % 60 )
	local millisecs = math.floor( ( seconds - math.floor( seconds ) ) * 1000 )
	seconds = math.floor( seconds % 60 )

	if ( format ) then
		return string_format( format, minutes, seconds, millisecs )
	else
		return { h = hours, m = minutes, s = seconds, ms = millisecs }
	end
end

--[[---------------------------------------------------------------------------
	Name: string.StartWith( str, start ) / string.StartsWith / string.EndsWith
	Desc: Prefix/suffix checks.
-----------------------------------------------------------------------------]]
function string.StartsWith( str, start )
	return string_sub( str, 1, string_len( start ) ) == start
end
string.StartWith = string.StartsWith

function string.EndsWith( str, endStr )
	return endStr == "" or string_sub( str, -string_len( endStr ) ) == endStr
end

--[[---------------------------------------------------------------------------
	Name: string.Implode( separator, table )
	Desc: Joins a table of values into one string.
-----------------------------------------------------------------------------]]
function string.Implode( separator, tbl )
	return table.concat( tbl, separator )
end

--[[---------------------------------------------------------------------------
	Name: string.Left( str, num ) / string.Right( str, num )
	Desc: Left/right substrings.
-----------------------------------------------------------------------------]]
function string.Left( str, num )
	return string_sub( str, 1, num )
end

function string.Right( str, num )
	return string_sub( str, -num )
end
