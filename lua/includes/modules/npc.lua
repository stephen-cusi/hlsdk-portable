--[[---------------------------------------------------------------------------
	npc.lua -- XGLua scripted NPC registry (SERVER only, M7-7D)
	Contracts:
	- Scripts in lua/npcs/ MUST explicitly call npc.Register(NPC, "classname")
	  at the bottom of the file.
	- Any sound path passed to self:EmitSound(...) MUST be listed in
	  NPC.PrecacheSounds so GoldSrc precaches it at map startup.
-----------------------------------------------------------------------------]]

if ( not SERVER ) then return end
if ( XGLUA_MODULE_NPC ) then return end
XGLUA_MODULE_NPC = true

npc = npc or {}

local StoredNPCs = {}

local function resolve( tbl, key )
	local seen = {}
	while tbl do
		if seen[ tbl ] then return nil end
		seen[ tbl ] = true
		local value = rawget( tbl, key )
		if value ~= nil then return value end
		local baseName = rawget( tbl, "Base" )
		tbl = type( baseName ) == "string" and StoredNPCs[ string.lower( baseName ) ] or nil
	end
end

local NPCBase = {
	ClassName = "npc_base",
	Base = false,
	Abstract = true,
	Model = "",
	Health = 100,
	YawSpeed = 180,
	Classify = 3, -- CLASS_HUMAN_MILITARY
	BloodColor = 247 -- BLOOD_COLOR_RED
}

function NPCBase:Initialize() end
function NPCBase:Think() return 0.1 end
function NPCBase:OnTakeDamage( attacker, inflictor, damage, dmgType ) end
function NPCBase:OnKilled( attacker, inflictor ) end

StoredNPCs.npc_base = NPCBase

function npc.Register( npc_table, className )
	if ( type( npc_table ) ~= "table" ) then
		print( "[XGLua] npc.Register: bad argument #1 (table expected, got " .. type( npc_table ) .. ")" )
		return nil
	end
	if ( className == nil ) then
		print( "[XGLua] npc.Register: bad argument #2 (string className expected, got nil)" )
		return nil
	end

	className = string.lower( tostring( className ) )
	npc_table.ClassName = className

	if className ~= "npc_base" and rawget( npc_table, "Base" ) == nil then
		npc_table.Base = "npc_base"
	end
	if rawget( npc_table, "Abstract" ) == nil then
		npc_table.Abstract = false
	end

	if getmetatable( npc_table ) ~= nil then
		print( "[XGLua] npc.Register: NPC tables with custom metatables are not supported" )
		return nil
	end

	setmetatable( npc_table, {
		__index = function( t, key )
			local base = rawget( t, "Base" )
			local parent = type( base ) == "string" and StoredNPCs[ string.lower( base ) ] or nil
			if key == "BaseClass" then return parent end
			return resolve( parent, key )
		end
	} )

	StoredNPCs[ className ] = npc_table
	return npc_table
end

function npc.Get( className )
	if ( className == nil ) then return nil end
	return StoredNPCs[ string.lower( tostring( className ) ) ]
end

function npc.GetStored( className )
	if ( className == nil ) then return StoredNPCs end
	return StoredNPCs[ string.lower( tostring( className ) ) ]
end

function npc.GetList()
	local list = {}
	local n = 0
	for _, entry in pairs( StoredNPCs ) do
		n = n + 1
		list[ n ] = entry
	end
	return list
end
