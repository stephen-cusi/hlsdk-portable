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


-- Computes visual bodygroup for a given clip count via optional SWEP:BodyFromClip(clip).
function WeaponBase:GetBodyForClip( clip )
	if type( self.BodyFromClip ) == "function" then
		local b = tonumber( self:BodyFromClip( clip ) ) or 0
		b = math.floor( b )
		if b < 0 then return 0 end
		if b > 255 then return 255 end
		return b
	end
	if type( self.GetBody ) == "function" then
		return self:GetBody() or 0
	end
	return 0
end

function WeaponBase:UpdateBodyFromClip()
	local body = self:GetBodyForClip( self:Clip1() )
	if type( self.SetBody ) == "function" then
		self:SetBody( body )
	end
	return body
end

-- Starts a two-phase / three-stage reload (e.g. M249 open cover -> fill belt -> close cover).
-- cfg = { OpenAnim, OpenDelay, CloseAnim, CloseDelay }
function WeaponBase:StartSpecialReload( cfg )
	cfg = cfg or self.SpecialReload
	if type( cfg ) ~= "table" then return false end
	if ( self._SpecialReloadStage or 0 ) > 0 or self:IsReloading() then
		return false
	end

	local maxClip = self:GetMaxClip1()
	if maxClip <= 0 or self:Clip1() >= maxClip or self:Ammo1() <= 0 then
		return false
	end

	local openAnim  = cfg.OpenAnim or 0
	local openDelay = cfg.OpenDelay or 1.0
	local body      = self:GetBodyForClip( self:Clip1() )

	if not self:DefaultReload( maxClip, openAnim, openDelay, body ) then
		return false
	end

	self._SpecialReloadStage = 1
	self._SpecialReloadCfg   = cfg
	self._SpecialReloadUntil = 0
	return true
end

-- Called by the native reload dispatcher before it transfers ammunition.
-- Display the prospective belt while the real inventory stays unchanged.
function WeaponBase:AdvanceSpecialReload()
	if self._SpecialReloadStage ~= 1 or not self:IsReloading() then return end
	local cfg = self._SpecialReloadCfg or {}
	local clip = self:Clip1()
	local preview = clip + math.min( math.max( 0, self:GetMaxClip1() - clip ), self:Ammo1() )
	local body = self:GetBodyForClip( preview )
	if type( self.SetBody ) == "function" then self:SetBody( body ) end
	local closeDelay = cfg.CloseDelay or 0
	if cfg.CloseAnim ~= nil then self:SendWeaponAnim( cfg.CloseAnim, body ) end
	if closeDelay > 0 then
		local readyAt = CurTime() + closeDelay
		self:SetNextPrimaryFire( readyAt )
		self:SetNextSecondaryFire( readyAt )
		self:SetNextAttack( closeDelay )
		self:SetWeaponIdleTime( readyAt )
	end
	self._SpecialReloadStage = 2
	self._SpecialReloadUntil = CurTime() + closeDelay
end

-- The clip is actually filled by native code after the closing animation.
function WeaponBase:ThinkSpecialReload()
	local stage = self._SpecialReloadStage or 0
	if stage == 0 then return false end
	if self:IsReloading() then return true end
	if stage == 2 then
		local body = self:UpdateBodyFromClip()
		if type( self.OnReloadClipFilled ) == "function" then self:OnReloadClipFilled( body ) end
		self._SpecialReloadStage = 0
		self._SpecialReloadCfg = nil
		self._SpecialReloadUntil = 0
		if type( self.OnReloadComplete ) == "function" then self:OnReloadComplete() end
		return false
	end
	-- A cancelled native reload must not leave the weapon permanently locked.
	self._SpecialReloadStage = 0
	self._SpecialReloadCfg = nil
	self._SpecialReloadUntil = 0
	return false
end

function WeaponBase:Holster()
	if ( self._SpecialReloadStage or 0 ) > 0 then self:UpdateBodyFromClip() end
	self._SpecialReloadStage = 0
	self._SpecialReloadCfg   = nil
	self._SpecialReloadUntil = 0
end

-- The native helper starts a timed reload and transfers ammo when it finishes.
function WeaponBase:Reload()
	if self:ThinkSpecialReload() then
		return false
	end
	if type( self.SpecialReload ) == "table" then
		return self:StartSpecialReload( self.SpecialReload )
	end
	local clipSize = self:GetMaxClip1()
	if clipSize <= 0 then return false end
	local body = self:GetBodyForClip( self:Clip1() )
	return self:DefaultReload(clipSize, self.ReloadAnimation or 0, self.ReloadTime or 2, body)
end

StoredWeapons.weapon_base = WeaponBase

--[[---------------------------------------------------------------------------
	Name: weapons.Register( swep_table, className )
--	Desc: Registers a SWEP table under the given class name. The table's
--		ClassName field is set to className (normalized to lower case,
--		matching GMod). Re-registering replaces the previous table.
--
--	Contracts (M5 P1):
--	- Scripts in lua/weapons/ MUST explicitly call weapons.Register(SWEP, "class")
--	  at the end of the file; folder loading does not auto-register.
--	- Any sound path passed to self:EmitSound(...) MUST be listed in
--	  SWEP.PrecacheSounds so GoldSrc precaches it on the server; unprecached
--	  sounds are silently dropped by the engine.
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


-- Default idle cadence with occasional longidle/fidget insertion (GMod style).
-- Weapons can override IdleSeq / LongIdleSeq / LongIdleEvery / IdleMinDelay / IdleMaxDelay.
function WeaponBase:WeaponIdle()
	if self:ThinkSpecialReload() then
		return
	end
	if CurTime() < ( self._NextIdleTime or 0 ) then
		return
	end

	local idleSeq     = self.IdleSeq or 1
	local longIdleSeq = self.LongIdleSeq
	local every       = math.max( 1, math.floor( self.LongIdleEvery or 3 ) )
	local minDelay    = self.IdleMinDelay or 3.5
	local maxDelay    = self.IdleMaxDelay or 6.0
	if maxDelay < minDelay then
		maxDelay = minDelay
	end

	self._IdleCount = ( self._IdleCount or 0 ) + 1

	if longIdleSeq and ( self._IdleCount >= every ) then
		self._IdleCount = 0
		self:SendWeaponAnim( longIdleSeq )
		local extra = self.LongIdleExtraDelay or 1.5
		self._NextIdleTime = CurTime() + minDelay + extra + math.random() * ( maxDelay - minDelay )
	else
		self:SendWeaponAnim( idleSeq )
		self._NextIdleTime = CurTime() + minDelay + math.random() * ( maxDelay - minDelay )
	end
end
