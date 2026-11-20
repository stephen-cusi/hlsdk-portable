# 游戏模式与共享能力

XGLua 将引擎会话、原生 GameRules、Lua 游戏模式与共享内容分开。`InstallGameRules` 仍由 HL1 逻辑决定；Lua 不更改 `deathmatch`、`coop`、最大玩家数或原生 GameRules。服务端地图启动时记录 `XGLUA_MODE_CONTEXT`，包括 `nativeRules`、`compatibility`、引擎 `deathmatch`/`coop`、`maxClients` 和 `requestedMode`。这是供脚本读取的启动快照，修改 Lua 表不会改变引擎；`requestedMode` 不是加载成功的保证；目前该上下文尚未同步到客户端。

`xgl_gamemode` 为空时，单人默认 `campaign`（空壳，保留原生单人行为），普通多人默认 `deathmatch`（继承 `base`）；团队、Busters、coop 当前不自动选择 Lua 模式。`xgl_gamemode none` 明确关闭 Lua 游戏模式。模式在地图启动时解析；不要在运行中的地图切换原生规则。将模式元数据中的 `rules` 设为 `singleplayer`、`multiplayer`、`teamplay`、`busters`、`coop` 或 `any`。没有 `rules` 的旧模式视为 `multiplayer`。整个继承链在执行任何游戏模式脚本之前检查，底座不兼容则拒绝加载并回退原生规则。coop 作为独立兼容类别处理，不暗示它已支持合作战役。

武器、实体、NPC 和通用库不应放在沙盒专属模式中。它们保持在公共内容层，沙盒 Spawnmenu 将来只是调用它们的一个入口；战役可以通过单人适用的获取方式使用同一个内容，但单人拾取、预测、存档和具体武器仍需逐项实机验证。服务端始终负责授予和生成。

`capabilities` 是各 Lua VM 独立的**服务发现**注册表，而不是插件安装器、依赖解析器或安全边界。每端都必须自行加载和注册自己实现的服务；不能假定服务端注册后客户端自动拥有它。目前核心仅注册 `xglua.weapons`（两端注册表）和 `xglua.vgui`（仅客户端）。注册名全局唯一，版本为正整数；同名重复注册报错，`Get(name, minVersion)` 返回服务或 nil。注册不代表实体、武器的所有引擎行为都在所有模式可用。

```lua
local api = capabilities.Get("example.shared_weapon_helper", 1)
if api then
    api.DoThing()
else
    -- 明确禁用依赖该能力的功能，或报告缺失；不要假装加载成功。
end
```

沙盒特有服务（例如物体生成权限、工具状态）应放在单独能力中并显式检查；确实需要沙盒行为的插件在 campaign 缺失时应禁用相应功能并提示，而不是自动打开 Spawnmenu 或修改战役规则。后续尚需实现插件包发现、依赖版本解析、加载顺序、热重载清理、客户端分发、沙盒生成入口以及模式切换协议；当前没有这些功能。

单人存档、换图、死亡重读，以及实际 SWEP 给予、HUD 和预测是否在各环境有效，必须由玩家分别验证；当前宿主测试不等于战役实机验收。
