--[[---------------------------------------------------------------------------
	weapons.lua -- XGLua weapon registry (GMod-compatible, simplified)
	Stores SWEP tables by class name. weapons.GetStored() returns the raw
	registered table; weapons.Get() currently returns the same table (a
	sanitized copy would be handed out in full GMod) and weapons.GetList()
	returns an array of all registered tables with their ClassName filled.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_WEAPONS ) then return end
XGLUA_MODULE_WEAPONS = true

weapons = weapons or {}

-- The raw stored registry: className -> SWEP table
local StoredWeapons = {}

--[[---------------------------------------------------------------------------
	Name: weapons.Register( swep_table, className )
	Desc: Registers a SWEP table under the given class name. The table's
		ClassName field is set to className (normalized to lower case,
		matching GMod). Re-registering replaces the previous table.
-----------------------------------------------------------------------------]]
function weapons.Register( swep_table, className )
	if ( type( swep_table ) ~= "table" ) then
		print( "[XGLua] weapons.Register: bad argument #1 (table expected, got " .. type( swep_table ) .. ")" )
		return
	end
	if ( className == nil ) then
		print( "[XGLua] weapons.Register: bad argument #2 (string className expected, got nil)" )
		return
	end

	className = string.lower( tostring( className ) )

	-- GMod stamps the class name onto the table
	swep_table.ClassName = className

	StoredWeapons[ className ] = swep_table

	return swep_table
end

--[[---------------------------------------------------------------------------
	Name: weapons.Get( className )
	Desc: Returns the registered weapon table for the class name (nil if
		not registered).
-----------------------------------------------------------------------------]]
function weapons.Get( className )
	if ( className == nil ) then return nil end
	return StoredWeapons[ string.lower( tostring( className ) ) ]
end

--[[---------------------------------------------------------------------------
	Name: weapons.GetStored( className )
	Desc: Called without arguments returns the whole stored registry table.
		Called with a class name returns that weapon's raw stored table.
-----------------------------------------------------------------------------]]
function weapons.GetStored( className )
	if ( className == nil ) then return StoredWeapons end
	return StoredWeapons[ string.lower( tostring( className ) ) ]
end

--[[---------------------------------------------------------------------------
	Name: weapons.GetList()
	Desc: Returns a sequentially indexed table of all registered weapon
		tables.
-----------------------------------------------------------------------------]]
function weapons.GetList()
	local list = {}
	local n = 0
	for _, swep in pairs( StoredWeapons ) do
		n = n + 1
		list[ n ] = swep
	end
	return list
end
