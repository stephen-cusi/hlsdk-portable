# 服务端 Trace 查询

`util.TraceLine(options)` 与 `util.TraceHull(options)` 是 HL1 原生 trace 的只读绑定。两者返回一个新的 Lua 结果表。

```lua
local tr = util.TraceLine({
	start = ply:ShootPos(),
	endpos = ply:ShootPos() + ply:GetAimVector() * 512,
	filter = ply,
	ignoremonsters = false,
})

local hull = util.TraceHull({
	start = ply:GetPos(),
	endpos = ply:GetPos() + Vector(0, 0, 64),
	filter = ply,
	hull = HULL_HUMAN,
})
```

## 参数

| 字段 | 类型与含义 |
|---|---|
| `start` | 必填，有限的 Vector，世界空间起点。 |
| `endpos` | 必填，有限的 Vector，世界空间终点。 |
| `filter` | 可选，只接受单个有效 Entity 或 nil。对应引擎的单实体跳过参数；表、字符串、函数过滤器均不支持，已删除的实体句柄会报错。 |
| `ignoremonsters` | 可选 boolean，默认 false；直接映射 HL1 trace 的 `ignore_monsters` / `MOVE_NOMONSTERS` 位。它是原生碰撞筛选，不是精确的 NPC 类别过滤：仍检测 BSP，并保留引擎的 pushstep 等特殊处理，也不等同于 Source collision mask。 |
| `hull` | 仅 TraceHull 必填，0..3 的整数；`HULL_POINT=0`、`HULL_HUMAN=1`、`HULL_LARGE=2`、`HULL_SMALL_CENTERED=HULL_HEAD=3`。 |

只接受上述字段。`mask`、`collisiongroup`、`mins/maxs`、自定义 `output` 表、其他未知字段均显式报错；不实现任意尺寸 hull 或 Source mask 语义。参数从表中直接读取，不调用 `__index`；请将字段直接存入 options 表。

端点各分量还限制为 `abs(value) <= sqrt(sqrt(FLT_MAX))/16`（约 2.68 亿单位）。原生球体碰撞宽阶段含四次量级的浮点表达式，只有 finite 检查仍会允许溢出；此界限为输入数值安全保留余量，不是 HL1 地图尺寸或 BSP hull 限制。

## 返回结果

| 字段 | 含义 |
|---|---|
| `Hit` | `Fraction < 1` 或原生 StartSolid/AllSolid 为真。 |
| `Fraction` | 引擎返回的行程比例；1 表示完成整个行程。 |
| `HitPos` | 引擎返回的世界空间终点 Vector 副本。 |
| `HitNormal` | 命中面的世界法线 Vector 副本。没有命中、StartSolid 或 AllSolid 时为零向量，不能作为有效命中面。 |
| `Entity` | 命中的有效实体句柄；世界命中为 `Entity(0)`。未命中时为 nil，即使某引擎在 miss 时返回 world pHit，也不会误报世界命中。 |
| `StartSolid` / `AllSolid` | 原生起点在 solid 内 / 整段处于 solid 内的标志。 |
| `HitWorld` / `HitNonWorld` | 命中实体分别为世界 / 非世界；未命中时均为 false。 |
| `HitGroup` | 原生命中组编号；未命中时为 0。 |
| `InOpen` / `InWater` | 原生 trace 标志，不代表 HitPos 的完整 contents 分类。 |

此版本不提供 `HitSky`、材质、SurfaceProps、FractionLeftSolid 等原生 TraceResult 无法直接提供的数据。TraceLine 与 HULL_POINT 可能因引擎内部路径不同而出现边缘差异，不能把二者视作所有情况下完全等价。

`filter = Entity(0)` 沿用引擎的 skip 参数，并不提供跳过世界 BSP 碰撞的能力。

查询不创建、移动、伤害或删除实体。引擎 trace API 本身可能更新原生全局 trace 状态；本绑定不调用 `UTIL_MakeVectors`，不会主动改写全局方向向量。
