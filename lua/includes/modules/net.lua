--[[---------------------------------------------------------------------------
	net.lua -- High-level net helpers (WriteTable / ReadTable)
	Built on top of the C++ net.Write*/Read* bitstream primitives.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_NET ) then return end
XGLUA_MODULE_NET = true

net = net or {}

local TYPE_END    = 0
local TYPE_BOOL   = 1
local TYPE_NUMBER = 2
local TYPE_STRING = 3
local TYPE_TABLE  = 4

local MAX_DEPTH   = 8
local MAX_ENTRIES = 1024

local function writeValue( val, depth, seen, state )
	local t = type( val )
	if ( t == "boolean" ) then
		net.WriteUInt( TYPE_BOOL, 3 )
		net.WriteBool( val )
	elseif ( t == "number" ) then
		net.WriteUInt( TYPE_NUMBER, 3 )
		net.WriteDouble( val )
	elseif ( t == "string" ) then
		net.WriteUInt( TYPE_STRING, 3 )
		net.WriteString( val )
	elseif ( t == "table" ) then
		if ( depth >= MAX_DEPTH ) then
			error( "net.WriteTable: max nesting depth (" .. MAX_DEPTH .. ") exceeded" )
		end
		if ( seen[ val ] ) then
			error( "net.WriteTable: cyclic table reference detected" )
		end
		net.WriteUInt( TYPE_TABLE, 3 )
		seen[ val ] = true
		for k, v in pairs( val ) do
			local kt, vt = type( k ), type( v )
			local kOk = ( kt == "number" or kt == "string" or kt == "boolean" )
			local vOk = ( vt == "number" or vt == "string" or vt == "boolean" or vt == "table" )
			if ( kOk and vOk ) then
				state.count = state.count + 1
				if ( state.count > MAX_ENTRIES ) then
					seen[ val ] = nil
					error( "net.WriteTable: max entries (" .. MAX_ENTRIES .. ") exceeded" )
				end
				writeValue( k, depth + 1, seen, state )
				writeValue( v, depth + 1, seen, state )
			end
		end
		seen[ val ] = nil
		net.WriteUInt( TYPE_END, 3 )
	else
		error( "net.WriteTable: unsupported type '" .. t .. "'" )
	end
end

local function readValue( depth, state )
	local tag = net.ReadUInt( 3 )
	if ( tag == TYPE_END ) then
		return nil, true
	elseif ( tag == TYPE_BOOL ) then
		return net.ReadBool(), false
	elseif ( tag == TYPE_NUMBER ) then
		return net.ReadDouble(), false
	elseif ( tag == TYPE_STRING ) then
		return net.ReadString(), false
	elseif ( tag == TYPE_TABLE ) then
		if ( depth >= MAX_DEPTH ) then
			error( "net.ReadTable: max nesting depth (" .. MAX_DEPTH .. ") exceeded" )
		end
		local tbl = {}
		while true do
			local k, isEnd = readValue( depth + 1, state )
			if ( isEnd ) then break end
			state.count = state.count + 1
			if ( state.count > MAX_ENTRIES ) then
				error( "net.ReadTable: max entries (" .. MAX_ENTRIES .. ") exceeded" )
			end
			local v, vEnd = readValue( depth + 1, state )
			if ( vEnd ) then break end
			if ( k ~= nil ) then
				tbl[ k ] = v
			end
		end
		return tbl, false
	end
	return nil, true
end

function net.WriteTable( tbl )
	if ( type( tbl ) ~= "table" ) then
		error( "net.WriteTable: table expected, got " .. type( tbl ) )
	end
	writeValue( tbl, 0, {}, { count = 0 } )
end

function net.ReadTable()
	local val = readValue( 0, { count = 0 } )
	if ( type( val ) ~= "table" ) then return {} end
	return val
end
