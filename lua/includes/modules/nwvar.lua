--[[---------------------------------------------------------------------------
	nwvar.lua -- GMod-compatible NetworkVar(type, slot, name) helper
	Generates Get<Name> and Set<Name> backed by SetNW* / GetNW*.
-----------------------------------------------------------------------------]]

if ( XGLUA_MODULE_NWVAR ) then return end
XGLUA_MODULE_NWVAR = true

local VALID_TYPES = {
	Bool   = { set = "SetNWBool",   get = "GetNWBool",   def = false },
	Int    = { set = "SetNWInt",    get = "GetNWInt",    def = 0 },
	Float  = { set = "SetNWFloat",  get = "GetNWFloat",  def = 0 },
	String = { set = "SetNWString", get = "GetNWString", def = "" },
	Vector = { set = "SetNWVector", get = "GetNWVector", def = nil },
	Angle  = { set = "SetNWAngle",  get = "GetNWAngle",  def = nil },
	Entity = { set = "SetNWEntity", get = "GetNWEntity", def = nil }
}

function XGLua_InstallNetworkVar( target, varType, slot, name )
	local info = VALID_TYPES[ varType ]
	if ( not info ) then
		error( "NetworkVar: unsupported type '" .. tostring( varType ) .. "'" )
	end
	if ( type( slot ) ~= "number" or slot < 0 or slot > 31 or math.floor( slot ) ~= slot ) then
		error( "NetworkVar: slot must be an integer 0..31" )
	end
	if ( type( name ) ~= "string" or name == "" ) then
		error( "NetworkVar: name must be a non-empty string" )
	end

	local key = "_nv_" .. tostring( slot ) .. "_" .. name
	local getter = info.get
	local setter = info.set
	local defVal = info.def

	target[ "Get" .. name ] = function( self )
		return self[ getter ]( self, key, defVal )
	end

	target[ "Set" .. name ] = function( self, value )
		self[ setter ]( self, key, value )
	end
end

-- Attach to META_ENT so entities / ENT tables can call self:NetworkVar(...)
META_ENT = META_ENT or {}
META_ENT.NetworkVar = XGLua_InstallNetworkVar
