-- gamemodes/base/gamemode/shared.lua
-- Base gamemode shared metadata & defaults (loaded before init.lua by xglua_gamemode.h).

GM.Name      = "Base"
GM.Author    = "XGLua"
GM.Email     = ""
GM.Website   = ""
GM.TeamBased = false

-- 对应 CGameRules::FAllowMonsters
-- 返回 nil 时回退至原版 mp_allowmonsters cvar 决定
function GM:AllowMonsters()
	return nil
end

function GM:Initialize()
end
