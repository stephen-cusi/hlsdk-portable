-- gamemodes/base/gamemode/init.lua
-- Base gamemode server rules. Returning nil from any decision callback
-- falls back cleanly to CHalfLifeMultiplay's vanilla behavior.

-- 对应 CHalfLifeMultiplay::PlayerSpawn 默认装备（受 base_loadout_test.cpp 回归保护）
function GM:PlayerLoadout( ply )
	if not ply then return false end
	if IsValid and not IsValid( ply ) then return false end

	ply:Give( "weapon_crowbar" )
	ply:Give( "weapon_9mmhandgun" )
	ply:GiveAmmo( 68, "9mm", 250 )
	return true
end

-- 生命周期通知（子类可覆写并通过 self.BaseClass 调用）
function GM:InitPostEntity()
end

function GM:PlayerInitialSpawn( ply )
end

function GM:PlayerSpawn( ply )
end

function GM:PlayerDeath( victim, inflictor, attacker )
end

function GM:PlayerDisconnected( ply )
end

-- 以下决策钩子默认返回 nil，交由 CLuaGameRules 回退至 CHalfLifeMultiplay 原版逻辑：
--   PlayerSelectSpawn( ply )              -> 返回出生点 Entity 覆写，nil 回退原版
--   PlayerCanRespawn( ply )               -> 严格 bool 覆写，nil 回退原版
--   PlayerSpawnTime( ply )                -> 有限浮点数（秒）覆写，nil 回退原版
--   GetFallDamage( ply )                  -> 有限浮点数伤害值覆写，nil 回退原版
--   GetPointsForKill( attacker, victim )  -> 整数分值覆写，nil 回退原版
--   PlayerSwitchWeapon( ply, old, new )   -> 严格 bool 覆写，nil 回退原版
--   ShouldAutoAim( ply, target )          -> 严格 bool 覆写，nil 回退原版
function GM:PlayerSelectSpawn( ply )
	return nil
end

function GM:PlayerCanRespawn( ply )
	return nil
end

function GM:PlayerSpawnTime( ply )
	return nil
end

function GM:GetFallDamage( ply )
	return nil
end

function GM:GetPointsForKill( attacker, victim )
	return nil
end

function GM:PlayerSwitchWeapon( ply, oldWeapon, newWeapon )
	return nil
end

function GM:ShouldAutoAim( ply, target )
	return nil
end
