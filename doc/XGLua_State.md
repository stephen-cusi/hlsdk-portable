# 服务端实体和玩家状态

| 方法 | 返回值 |
|---|---|
| `ent:GetMoveType()` | 原生 `movetype` 整数，可与 `MOVETYPE_*` 比较。 |
| `ent:GetSolid()` | 原生 `solid` 整数，可与 `SOLID_*` 比较。 |
| `ent:WaterLevel()` | 原生 `waterlevel` 整数，通常为 0 至 3。 |
| `ent:OnGround()` / `ent:IsOnGround()` | 实体是否有 `FL_ONGROUND` 标记。两个名称等价。 |
| `ply:Crouching()` | 玩家是否已进入 `FL_DUCKING` 状态。 |
| `ply:Armor()` | 原生护甲值，保留小数。 |
| `ply:Ping()` | 引擎报告的毫秒延迟。引擎未提供统计值时为 0。 |
| `ply:KeyDown(mask)` | 当前玩家 `button` 状态与 `mask` 有至少一个共同位时返回 true。 |
| `ply:KeyPressed(mask)` / `ply:KeyReleased(mask)` | 分别读取 `CBasePlayer::PreThink()` 上次计算的按下和释放位，所查掩码有至少一个匹配位时返回 true。 |

三个输入方法接受 0 至 65535 的整数掩码，常用按钮以 `IN_USE`、`IN_JUMP`、`IN_DUCK` 等全局常量提供。掩码 0 始终返回 false。按下和释放是引擎在玩家命令处理时更新的快照，同一阶段内可重复读取。控制台执行脚本的时点可能错过瞬时变化。
