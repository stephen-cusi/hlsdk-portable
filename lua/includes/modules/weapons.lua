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

-- Resolve from raw fields so a malformed or cyclic Base chain cannot recurse
-- indefinitely through Lua metatables. Bases may be registered in any order.
local function resolve( swep, key )
    local seen = {}
    while swep do
        if seen[ swep ] then return nil end
        seen[ swep ] = true
        local value = rawget( swep, key )
        if value ~= nil then return value end
        local name = rawget( swep, "Base" )
        swep = type( name ) == "string" and StoredWeapons[ string.lower( name ) ] or nil
    end
end

local WeaponBase = {
    ClassName = "weapon_base",
    Base = false,
    Abstract = true,
    Primary = { ClipSize = -1, DefaultClip = 0, Ammo = "none", Automatic = false },
    Secondary = { ClipSize = -1, DefaultClip = 0, Ammo = "none", Automatic = false }
}

-- Shared P1 helpers use only entity/weapon methods provided by the server.
-- A negative ClipSize means reserve-only ammunition; "none" means unlimited.
function WeaponBase:Ammo1()
    local primary = self.Primary or {}
    local ammo = primary.Ammo
    if type(ammo) ~= "string" or ammo == "" or ammo == "none" then return 0 end
    local owner = self:GetOwner()
    if not IsValid(owner) or not owner:IsPlayer() then return 0 end
    return owner:GetAmmoCount(ammo)
end

-- GoldSrc secondary ammunition is reserve-only; it has no second clip.
function WeaponBase:Ammo2()
    local secondary = self.Secondary or {}
    local ammo = secondary.Ammo
    if type(ammo) ~= "string" or ammo == "" or ammo == "none" then return 0 end
    local owner = self:GetOwner()
    if not IsValid(owner) or not owner:IsPlayer() then return 0 end
    return owner:GetAmmoCount(ammo)
end

function WeaponBase:CanSecondaryAttack()
    local secondary = self.Secondary or {}
    local ammo = secondary.Ammo
    if ammo == nil or ammo == "" or ammo == "none" or self:Ammo2() > 0 then
        return true
    end
    self:PlayEmptySound()
    self:SetNextSecondaryFire(CurTime() + 0.2)
    return false
end

function WeaponBase:GetMaxClip1()
    local primary = self.Primary or {}
    return primary.ClipSize or -1
end

function WeaponBase:CanPrimaryAttack()
    local primary = self.Primary or {}
    local clipSize = self:GetMaxClip1()
    local ammo = primary.Ammo
    local hasAmmo = clipSize < 0 and
        (ammo == nil or ammo == "" or ammo == "none" or self:Ammo1() > 0) or
        (clipSize >= 0 and self:Clip1() > 0)
    if hasAmmo then return true end
    self:PlayEmptySound()
    self:SetNextPrimaryFire(CurTime() + 0.2)
    return false
end

-- The native helper starts a timed reload and transfers ammo when it finishes.
function WeaponBase:Reload()
    local clipSize = self:GetMaxClip1()
    if clipSize <= 0 then return false end
    return self:DefaultReload(clipSize, self.ReloadAnimation or 0, self.ReloadTime or 2)
end

StoredWeapons.weapon_base = WeaponBase

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
    if className ~= "weapon_base" and rawget( swep_table, "Base" ) == nil then
        swep_table.Base = "weapon_base"
    end
    if rawget( swep_table, "Abstract" ) == nil then swep_table.Abstract = false end
    local previous = getmetatable( swep_table )
    if previous ~= nil then
        print( "[XGLua] weapons.Register: SWEP tables with custom metatables are not supported" )
        return nil
    end
    setmetatable( swep_table, { __index = function( table_, key )
        local base = rawget( table_, "Base" )
        local parent = type( base ) == "string" and StoredWeapons[ string.lower( base ) ] or nil
        if key == "BaseClass" then return parent end
        return resolve( parent, key )
    end } )

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
