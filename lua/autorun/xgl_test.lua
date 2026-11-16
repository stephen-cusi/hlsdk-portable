--[[---------------------------------------------------------
    XGLua 验证脚本 (autorun)
    ---------------------------------------------------------
    放在 lua/autorun/ 下, XGLua 启动时自动执行 (SERVER 侧)。
    做三件事:
      1. 打印启动横幅, 确认 Lua 系统活了
      2. hook.Add("Think", ...) 只在第一帧打印 CurTime, 然后自删
         (验证 hook.Add / hook.Remove 链路)
      3. InitPostEntity 后 2 秒自测 SWEP 实体创建链路
         (验证 ents.Create -> CLuaWeapon placement-new -> IsValid)

    开局不自动发武器: 服务端控制台输入
        sv_cheats 1
        give weapon_grenadepistol
    获取手雷手枪 (客户端按 LMB 接触手雷 / RMB 定时手雷)。
-------------------------------------------------------------]]

print( "[XGLua] autorun/xgl_test.lua loaded, CurTime=" .. CurTime() )
print( "[XGLua] SERVER=" .. tostring( SERVER ) .. " CLIENT=" .. tostring( CLIENT ) )

-- Think hook 自检: 第一帧打印后自删 (GMod 习惯写法)
hook.Add( "Think", "xgl_boot", function()
    hook.Remove( "Think", "xgl_boot" )
    print( "[XGLua] Think hook alive, CurTime=" .. CurTime() )
end )

-- SWEP 实体创建自测 (服务端运行期, CLuaWeapon placement-new 路径)
hook.Add( "InitPostEntity", "xgl_swep_selftest", function()
    timer.Simple( 2, function()
        local e = ents.Create( "weapon_grenadepistol" )
        if not IsValid( e ) then
            print( "[XGLua] SWEP self-test FAIL: ents.Create returned invalid entity" )
            return
        end
        print( "[XGLua] SWEP self-test OK:", e, e:GetClass(), e:EntIndex() )
        e:Remove()
    end )
end )
