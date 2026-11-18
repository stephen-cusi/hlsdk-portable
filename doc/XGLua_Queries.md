# 服务端地图、实体和玩家查询

| API | 返回值和语义 |
|---|---|
| `game.GetMap()` | 当前地图名字符串。 |
| `game.MaxPlayers()` | 当前服务器的玩家槽位上限。 |
| `game.GetWorld()` | 与 `Entity(0)` 相同的世界实体句柄。 |
| `ents.FindByName(name)` | 按 HL1 的 `targetname` 精确匹配，区分大小写。空字符串返回空表。 |
| `ents.FindInSphere(center, radius)` | 找出其世界轴对齐碰撞边界与球形区域相交的实体。`center` 是有限 Vector，`radius` 是非负有限数值。 |
| `ents.FindInBox(mins, maxs)` | 找出其世界轴对齐碰撞边界与查询盒相交的实体。两个 Vector 都必须有限，每个轴的 `mins` 必须小于或等于 `maxs`。 |
| `player.GetHumans()` / `player.GetBots()` | 从当前玩家槽位分别取得普通客户端和带 `FL_FAKECLIENT` 标记的客户端。旁观者仍属于其客户端类别。 |
| `ply:UserID()` | 引擎分配的正整数玩家用户 ID。引擎无法提供时返回 `nil`。 |
| `player.GetByUserID(id)` | 按正整数用户 ID 查找玩家，找不到时返回 `nil`。 |

所有列表都是按实体槽位排列的新 Lua 数组，索引从 1 开始。实体查找只返回已初始化且未标记删除的实体，世界实体可通过 `game.GetWorld()` 单独获取。空间查找包含接触查询边界的实体，读取引擎的 `absmin` 和 `absmax`。这些界限可能包含引擎预留的碰撞余量。查询会扫描实体槽位。

`UserID()` 来自引擎的 `GetPlayerUserId`，与实体槽位 `EntIndex()` 分开。可用 `player.GetByUserID(ply:UserID())` 查回一个仍在线且具有用户 ID 的玩家。
