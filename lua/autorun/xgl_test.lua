--[[---------------------------------------------------------
    XGLua 验证脚本 (autorun)
    ---------------------------------------------------------
    放在 lua/autorun/ 下, XGLua 启动时自动执行 (SERVER 侧)。
    做三件事:
      1. 打印启动横幅, 确认 Lua 系统活了
      2. hook.Add("Think", ...) 只在第一帧打印 CurTime, 然后自删
         (验证 hook.Add / hook.Remove 链路)
      3. hook.Add("PlayerSpawn", ...) 给每个出生的玩家发
         "weapon_grenadepistol" SWEP + 6 发 9mm 备弹
-----------------------------------------------------------]]

if SERVER then
    -- 启动横幅
    print("[XGLua] autorun/xgl_test.lua loaded, CurTime=" .. CurTime())
    print("[XGLua] SERVER=" .. tostring(SERVER) .. " CLIENT=" .. tostring(CLIENT))

    --[[-----------------------------------------------------
        Think hook: 引擎每帧回调一次。这里只打印第一帧的
        CurTime, 然后用 hook.Remove 把自己摘掉, 不留常驻开销。
        (对应 GMod hook.Add("Think", id, fn) / hook.Remove(event, id))
    -------------------------------------------------------]]
    hook.Add("Think", "xgl_boot", function()
        print("[XGLua] Think hook alive, CurTime=" .. CurTime())
        hook.Remove("Think", "xgl_boot")
    end)

    --[[-----------------------------------------------------
        PlayerSpawn hook: 玩家出生后延时 0.1 秒发武器。
        延迟的原因: 出生瞬间玩家的库存/弹药状态还没完全就绪,
        等一帧再 Give / GiveAmmo 更稳。
        timer.Simple 来自 includes/modules/timer.lua
        (对应 GMod timer.Simple(delay, fn))
    -------------------------------------------------------]]
    hook.Add("PlayerSpawn", "xgl_give_grenadepistol", function(ply)
        if not IsValid(ply) or not ply:IsPlayer() then return end

        timer.Simple(0.1, function()
            -- 0.1 秒后玩家可能已经断线或死亡, 先校验再发
            if not IsValid(ply) or not ply:IsPlayer() or not ply:Alive() then
                return
            end

            -- 对应 GMod ply:Give("weapon_class") — 实际调用 ents.Create
            -- + C++ 侧 GiveAmmo, 类名要在 weapons.Register 里注册过
            ply:Give("weapon_grenadepistol")

            -- 对应 GMod ply:GiveAmmo(count, ammoName) — 6 发备弹
            -- (弹匣容量 1 发在 SWEP 表里, 备弹走 HL1 "9mm" 弹药类型)
            ply:GiveAmmo(6, "9mm")

            -- 对应 GMod ply:ChatPrint(msg) — 发到玩家聊天栏
            ply:ChatPrint("Grenade Pistol given!")

            -- 服务端控制台也打一条, 方便确认
            print("[XGLua] Gave grenade pistol to " .. (ply:Nick() or "?"))
        end)
    end)
end

-- SWEP creation self-test (server runtime): CLuaWeapon placement-new path
hook.Add("InitPostEntity", "xgl_swep_selftest", function()
	timer.Simple(2, function()
		local e = ents.Create("weapon_grenadepistol")
		if not IsValid(e) then
			print("[XGLua] SWEP self-test FAIL: ents.Create returned invalid entity")
			return
		end
		print("[XGLua] SWEP self-test OK:", e, e:GetClass(), e:EntIndex())
		e:Remove()
	end)
end)
