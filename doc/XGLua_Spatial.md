# 实体与玩家空间查询

以下 API 在服务端可用，均返回独立的 Vector 或 Angle 副本，不会移动实体或改变碰撞盒。使用已删除实体的句柄会报 `NULL entity` 错误。

## HL1 坐标约定

`Entity:GetPos()` 保持 HL1 原生的 `pev->origin` 语义。站立玩家的原点在碰撞盒中心；通常本地 Z 边界为 `-36..36`，蹲伏时边界由游戏更新。查询不会将原点隐式改为 GMod 的脚底原点。

| 方法 | 返回值 | 语义 |
|---|---|---|
| `ent:GetFeetPos()` | Vector | X/Y 保持 origin，Z 为 `origin.z + mins.z`；对玩家是碰撞盒底面中心。它不是任意旋转模型的几何最低点。 |
| `ent:WorldSpaceCenter()` | Vector | 引擎世界边界 `absmin` 和 `absmax` 的中点，与 HL1 `CBaseEntity::Center()` 的默认实现相同；对 BSP 实体不使用 origin 加本地边界来猜测世界中心。 |
| `ent:OBBMins()` | Vector | 原生本地碰撞边界 `mins` 的副本。名字用于 API 兼容，不代表 HL1 支持任意大小的旋转玩家 hull。 |
| `ent:OBBMaxs()` | Vector | 原生本地碰撞边界 `maxs` 的副本。 |
| `ent:GetCollisionBounds()` | Vector, Vector | 同时返回本地 `mins, maxs`，顺序与上述两个方法相同。 |
| `ply:ShootPos()` | Vector | 调用原生 `CBasePlayer::GetGunPosition()`；当前分支为 `origin + view_ofs`，与玩家眼部位置相同。 |

这些方法不会改变 `SetPos`、`SetCollisionBounds` 或 BSP 编译 hull 的行为。

## 玩家视角

`ply:EyePos()` 返回 `origin + view_ofs`。`ply:EyeAngles()` 现在返回真正的 Angle，字段为 `p/y/r` 或 `pitch/yaw/roll`；它是原生 `v_angle` 的副本，不含 punchangle 和自动瞄准偏移。此前此方法错误地返回 Vector，依赖 `.x/.z` 的脚本需改用 `.p/.r`。

`ply:GetAimVector()` 保持原有未辅助瞄准方向：从 `v_angle` 得到单位前向 Vector，不叠加 punchangle 或自动瞄准。查询使用本地方向向量，不再覆盖 `gpGlobals` 的 `v_forward/v_right/v_up`。

本批保留旧的 `ent:GetAngles()` / `ent:SetAngles()` Vector 契约；因此不能把 `EyeAngles()` 返回的 Angle 直接传给当前 `SetAngles()`。后续若统一 Angle 契约，需要单独处理现有脚本兼容性。
