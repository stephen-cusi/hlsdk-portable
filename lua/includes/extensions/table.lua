--[[---------------------------------------------------------------------------
	table.lua -- XGLua table library extensions (GMod-compatible)
	Adds the GMod table.* helpers onto the standard Lua table library:
	Copy (deep, metatable + cycle aware), Merge, HasValue, Count, Random,
	GetKeys, Reverse, Empty, IsSequential, plus a few small extras.
	Ported from the official GMod implementation (Facepunch/garrysmod).
-----------------------------------------------------------------------------]]

if ( XGLUA_EXT_TABLE ) then return end
XGLUA_EXT_TABLE = true

local pairs = pairs
local type = type
local insert = table.insert
local remove = table.remove

local function istable( v )
	return type( v ) == "table"
end

--[[---------------------------------------------------------------------------
	Name: table.Copy( t )
	Desc: Deep-copies a table, preserving its metatable and handling
		recursive references (cycles) through a lookup table.
-----------------------------------------------------------------------------]]
function table.Copy( t, lookup_table )
	if ( t == nil ) then return nil end

	local copy = {}
	setmetatable( copy, getmetatable( t ) )

	for i, v in pairs( t ) do
		if ( not istable( v ) ) then
			copy[ i ] = v
		else
			lookup_table = lookup_table or {}
			lookup_table[ t ] = copy

			if ( lookup_table[ v ] ) then
				copy[ i ] = lookup_table[ v ] -- already copied: reuse it (cycle)
			else
				copy[ i ] = table.Copy( v, lookup_table )
			end
		end
	end

	return copy
end

--[[---------------------------------------------------------------------------
	Name: table.Empty( t )
	Desc: Removes every key from the table in place.
-----------------------------------------------------------------------------]]
function table.Empty( t )
	for k, v in pairs( t ) do
		t[ k ] = nil
	end
end

--[[---------------------------------------------------------------------------
	Name: table.IsEmpty( t )
	Desc: True if the table has no entries at all.
-----------------------------------------------------------------------------]]
function table.IsEmpty( t )
	return next( t ) == nil
end

--[[---------------------------------------------------------------------------
	Name: table.CopyFromTo( from, to )
	Desc: Makes 'to' exactly the same as 'from' while staying the same table.
-----------------------------------------------------------------------------]]
function table.CopyFromTo( from, to )
	table.Empty( to )
	table.Merge( to, from )
end

--[[---------------------------------------------------------------------------
	Name: table.Merge( dest, src )
	Desc: Copies all entries from src into dest. When both sides hold a
		table under the same key they are merged recursively instead of
		overwritten. Returns dest.
-----------------------------------------------------------------------------]]
function table.Merge( dest, src )
	for k, v in pairs( src ) do
		if ( istable( v ) and istable( dest[ k ] ) ) then
			-- don't overwrite one table with another; merge them recursively
			table.Merge( dest[ k ], v )
		else
			dest[ k ] = v
		end
	end

	return dest
end

--[[---------------------------------------------------------------------------
	Name: table.HasValue( t, val )
	Desc: True if any value in the table equals val.
-----------------------------------------------------------------------------]]
function table.HasValue( t, val )
	for k, v in pairs( t ) do
		if ( v == val ) then return true end
	end
	return false
end

--[[---------------------------------------------------------------------------
	Name: table.Count( t )
	Desc: Number of keys in the table (works for non-sequential tables too).
-----------------------------------------------------------------------------]]
function table.Count( t )
	local i = 0
	for k in pairs( t ) do
		i = i + 1
	end
	return i
end

--[[---------------------------------------------------------------------------
	Name: table.Random( t )
	Desc: Returns a random value (and its key) from the table.
-----------------------------------------------------------------------------]]
function table.Random( t )
	local rk = math.random( 1, table.Count( t ) )
	local i = 1
	for k, v in pairs( t ) do
		if ( i == rk ) then return v, k end
		i = i + 1
	end
end

--[[---------------------------------------------------------------------------
	Name: table.GetKeys( t )
	Desc: Returns a sequentially indexed array of all keys in the table.
-----------------------------------------------------------------------------]]
function table.GetKeys( t )
	local keys = {}
	local n = 0
	for k, v in pairs( t ) do
		n = n + 1
		keys[ n ] = k
	end
	return keys
end

--[[---------------------------------------------------------------------------
	Name: table.Reverse( t )
	Desc: Returns a new table with the sequential part reversed.
-----------------------------------------------------------------------------]]
function table.Reverse( t )
	local len = #t
	local ret = {}

	for i = len, 1, -1 do
		ret[ len - i + 1 ] = t[ i ]
	end

	return ret
end

--[[---------------------------------------------------------------------------
	Name: table.IsSequential( t )
	Desc: True if the table's keys form a 1..n sequence.
-----------------------------------------------------------------------------]]
function table.IsSequential( t )
	local i = 1
	for key, value in pairs( t ) do
		if ( t[ i ] == nil ) then return false end
		i = i + 1
	end
	return true
end

--[[---------------------------------------------------------------------------
	Name: table.Add( dest, src )  (GMod parity bonus)
	Desc: Appends all values of src onto dest, discarding keys.
-----------------------------------------------------------------------------]]
function table.Add( dest, src )
	if ( dest == src ) then return dest end

	if ( not istable( src ) ) then return dest end
	if ( not istable( dest ) ) then dest = {} end

	for k, v in pairs( src ) do
		insert( dest, v )
	end

	return dest
end

--[[---------------------------------------------------------------------------
	Name: table.KeyFromValue( t, val )  (GMod parity bonus)
	Desc: Returns the key holding val, or nil.
-----------------------------------------------------------------------------]]
function table.KeyFromValue( t, val )
	for key, value in pairs( t ) do
		if ( value == val ) then return key end
	end
end

--[[---------------------------------------------------------------------------
	Name: table.RemoveByValue( t, val )  (GMod parity bonus)
	Desc: Removes the entry holding val; returns the removed key or false.
-----------------------------------------------------------------------------]]
function table.RemoveByValue( t, val )
	local key = table.KeyFromValue( t, val )
	if ( not key ) then return false end

	if ( type( key ) == "number" ) then
		remove( t, key )
	else
		t[ key ] = nil
	end

	return key
end
