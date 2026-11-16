--[[---------------------------------------------------------------------------
	enum.lua -- XGLua HL1 engine constants
	MoveTypes, solid types, damage types, hull types, USE types, HUD print
	channels and match-result codes, exposed as globals (GMod style).
	Values follow the HL1 SDK (pm_defs.h / dlls/decals.h / util.h etc.).
-----------------------------------------------------------------------------]]

if ( XGLUA_ENUM_LOADED ) then return end
XGLUA_ENUM_LOADED = true

-- MOVETYPE_* -- entity movement types (HL1 pm_defs.h)
MOVETYPE_NONE        = 0     -- never moves
MOVETYPE_WALK        = 3     -- normal player movement
MOVETYPE_STEP        = 4     -- gravity, special edge handling (monsters)
MOVETYPE_FLY         = 5     -- no gravity, but keeps collisions
MOVETYPE_TOSS        = 6     -- gravity/collisions, bounces after coming to rest
MOVETYPE_PUSH        = 7     -- no clip to world, pushes entities it touches
MOVETYPE_NOCLIP      = 8     -- no gravity, no collisions
MOVETYPE_FLYMISSILE  = 9     -- extra flight to entity velocity (rockets)
MOVETYPE_BOUNCE      = 10    -- like FLYMISSILE, bounces off surfaces

-- SOLID_* -- entity solidity (HL1)
SOLID_NOT            = 0     -- no interaction with other objects
SOLID_TRIGGER        = 1     -- touch on edge, but not blocking
SOLID_BBOX           = 2     -- interact on bbox, not inside
SOLID_SLIDEBOX       = 3     -- try to slide up/bold surfaces, not step
SOLID_BSP            = 4     -- bsp poly, brush entity

-- DMG_* -- damage types (HL1, dlls/cbase.h style bitflags)
DMG_GENERIC          = 0
DMG_CRUSH            = 1
DMG_BULLET           = 2
DMG_SLASH            = 4
DMG_BURN             = 8
DMG_BLAST            = 32
DMG_CLUB             = 64
DMG_SHOCK            = 256
DMG_SONIC            = 512
DMG_FALL             = 1024
DMG_DROWN            = 2048
DMG_PARALYZE         = 4096
DMG_NERVEGAS         = 8192
DMG_POISON           = 16384
DMG_RADIATION        = 32768
DMG_ACID             = 65536
DMG_SLOWBURN         = 131072
DMG_SLOWFREEZE       = 262144

-- HULL_* -- collision hull sizes (HL1 util.h)
HULL_POINT           = 0     -- 0x0x0
HULL_HUMAN           = 1     -- standard player hull
HULL_LARGE           = 2     -- multiple of human hull (large monsters)
HULL_SMALL_CENTERED  = 3     -- small hull centered on origin

-- USE_* -- entity "use" output modes (HL1)
USE_OFF              = 0
USE_ON               = 1
USE_SET              = 2
USE_TOGGLE           = 3

-- HUD_PRINT* -- text print channels (HUD)
HUD_PRINTNOTIFY      = 1
HUD_PRINTCONSOLE     = 2
HUD_PRINTTALK        = 3
HUD_PRINTCENTER      = 4

-- MR_* -- "match result" / inventory-give query answers (HL1 GiveAmmo)
MR_INVALID           = -1
MR_AMMO              = 0     -- gave some ammo
MR_RETRY             = 1     -- can't take ammo now, try again later
MR_NOANSWER          = 2     -- no response
MR_NOT_CARRIED       = 3     -- ammo type not carried by this weapon
MR_NOAMMO            = 4     -- no ammo of this type
MR_COVER             = 5     -- (squad/leader queries) ordered to take cover
MR_NOTDISTRACT       = 6     -- (squad queries) no distraction possible
MR_NO_TARGET         = 7     -- (squad queries) no target available
MR_SQUAD             = 8     -- (squad queries) squad leader answered
