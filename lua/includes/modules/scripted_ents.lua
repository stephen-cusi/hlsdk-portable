--[[---------------------------------------------------------------------------
	scripted_ents.lua -- XGLua scripted entity registry (GMod-compatible)
	Stores ENT tables by class name with cycle-safe Base inheritance,
	matching the conventions of weapons.lua.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_SCRIPTED_ENTS ) then return end
XGLUA_MODULE_SCRIPTED_ENTS = true

scripted_ents = scripted_ents or {}

-- Raw stored registry: className (lowercase) -> ENT table
local StoredEntities = {}

-- Resolve from raw fields so a malformed or cyclic Base chain cannot recurse
-- indefinitely through Lua metatables. Bases may be registered in any order.
local function resolve( ent, key )
	local seen = {}
	while ent do
		if seen[ ent ] then return nil end
		seen[ ent ] = true
		local value = rawget( ent, key )
		if value ~= nil then return value end
		local name = rawget( ent, "Base" )
		ent = type( name ) == "string" and StoredEntities[ string.lower( name ) ] or nil
	end
	return nil
end

local BaseEntity = {
	ClassName = "base_entity",
	Type = "anim",
	Base = false,
	Abstract = true,
	Spawnable = false
}

local BaseAnim = {
	ClassName = "base_anim",
	Type = "anim",
	Base = "base_entity",
	Abstract = true,
	Spawnable = false
}

local BasePoint = {
	ClassName = "base_point",
	Type = "point",
	Base = "base_entity",
	Abstract = true,
	Spawnable = false
}

local function attachMetatable( tbl )
	setmetatable( tbl, {
		__index = function( table_, key )
			local base = rawget( table_, "Base" )
			local parent = type( base ) == "string" and StoredEntities[ string.lower( base ) ] or nil
			if key == "BaseClass" then return parent end
			return resolve( parent, key )
		end
	} )
end

attachMetatable( BaseAnim )
attachMetatable( BasePoint )

StoredEntities.base_entity = BaseEntity
StoredEntities.base_anim = BaseAnim
StoredEntities.base_point = BasePoint

--[[---------------------------------------------------------------------------
	Name: scripted_ents.Register( ent_table, className )
	Desc: Registers an ENT table under the given class name (normalized to
		lower case). Re-registering replaces the previous table.
-----------------------------------------------------------------------------]]
function scripted_ents.Register( ent_table, className )
	if ( type( ent_table ) ~= "table" ) then
		print( "[XGLua] scripted_ents.Register: bad argument #1 (table expected, got " .. type( ent_table ) .. ")" )
		return nil
	end
	if ( className == nil ) then
		print( "[XGLua] scripted_ents.Register: bad argument #2 (string className expected, got nil)" )
		return nil
	end

	className = string.lower( tostring( className ) )
	if ( className == "" ) then
		print( "[XGLua] scripted_ents.Register: className must not be empty" )
		return nil
	end

	local previous = getmetatable( ent_table )
	if ( previous ~= nil ) then
		print( "[XGLua] scripted_ents.Register: ENT tables with custom metatables are not supported" )
		return nil
	end

	ent_table.ClassName = className
	if ( className ~= "base_entity" and rawget( ent_table, "Base" ) == nil ) then
		ent_table.Base = "base_entity"
	end
	if ( rawget( ent_table, "Abstract" ) == nil ) then
		ent_table.Abstract = false
	end

	attachMetatable( ent_table )
	StoredEntities[ className ] = ent_table

	return ent_table
end

--[[---------------------------------------------------------------------------
	Name: scripted_ents.Get( className )
	Desc: Returns the registered ENT table for className (nil if absent).
-----------------------------------------------------------------------------]]
function scripted_ents.Get( className )
	if ( className == nil ) then return nil end
	return StoredEntities[ string.lower( tostring( className ) ) ]
end

--[[---------------------------------------------------------------------------
	Name: scripted_ents.GetStored( className )
	Desc: Called without arguments returns the whole stored registry table.
		Called with a class name returns that entity's raw stored table.
-----------------------------------------------------------------------------]]
function scripted_ents.GetStored( className )
	if ( className == nil ) then return StoredEntities end
	return StoredEntities[ string.lower( tostring( className ) ) ]
end

--[[---------------------------------------------------------------------------
	Name: scripted_ents.GetList()
	Desc: Returns a sequentially indexed array of all registered ENT tables.
-----------------------------------------------------------------------------]]
function scripted_ents.GetList()
	local list = {}
	local n = 0
	for _, ent in pairs( StoredEntities ) do
		n = n + 1
		list[ n ] = ent
	end
	return list
end

--[[---------------------------------------------------------------------------
	Name: scripted_ents.GetType( className )
	Desc: Returns the entity Type ("anim" / "point" / "brush") resolving Base.
-----------------------------------------------------------------------------]]
function scripted_ents.GetType( className )
	local ent = scripted_ents.Get( className )
	if ( ent == nil ) then return nil end
	return ent.Type
end
