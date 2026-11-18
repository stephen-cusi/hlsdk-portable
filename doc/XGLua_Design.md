# XGLua — Xash3D / HL1 的 GLua 风格 LuaJIT 脚本系统设计文档

> 版本：v0.2 草案（整合 TWHL / VDC / hlsdk-portable / Xash3D FWGS 资料后修订）　日期：2026-11-18
> 工作代号：**XGLua**（可随意改名）
> 目标一句话：**让 HL1 mod 作者像写 Garry's Mod 一样，用 Lua 写游戏模式、实体、武器、HUD 和网络逻辑。**

文中标有 ⚠️ 的地方表示"我基于对 GoldSrc / Xash3D 的理解做的判断，动手前需要对照 `xash3d-fwgs` 与 `hlsdk-portable` 源码验证"。

---

## 目录

1. [目标与非目标](#1-目标与非目标)
2. [总体架构决策](#2-总体架构决策)
3. [目录结构与文件约定](#3-目录结构与文件约定)
4. [Realm 与加载流程](#4-realm-与加载流程)
5. [LuaJIT 集成与构建](#5-luajit-集成与构建)
6. [数据类型与绑定层](#6-数据类型与绑定层)
7. [核心库 API 清单](#7-核心库-api-清单)
8. [Hook 映射表](#8-hook-映射表)
9. [脚本化实体 / 武器 / NPC / 游戏模式](#9-脚本化实体--武器--npc--游戏模式)
10. [网络系统](#10-网络系统)
11. [客户端渲染与 UI](#11-客户端渲染与-ui)
12. [GLua 语法兼容层](#12-glua-语法兼容层)
13. [沙箱与安全](#13-沙箱与安全)
14. [调试与开发体验](#14-调试与开发体验)
15. [示例脚本](#15-示例脚本)
16. [里程碑](#16-里程碑)
17. [风险与待验证清单](#17-风险与待验证清单)
18. [多平台与 Android](#18-多平台与-android)
19. [HL1 / GoldSrc 机制速查及其对设计的影响](#19-hl1--goldsrc-机制速查及其对设计的影响)
20. [参考资料](#20-参考资料)

---

## 1. 目标与非目标

### 1.1 目标

| # | 目标 | 说明 |
|---|------|------|
| G1 | GMod 风格 API | `hook` / `timer` / `net` / `concommand` / `ents` / `player` / `util` / `team` / `file` 等库，函数名与签名尽量与 GMod 一致 |
| G2 | Realm 模型 | 服务端 / 客户端两个独立 Lua VM，全局 `SERVER`、`CLIENT`，`AddCSLuaFile()`、`include()` 行为一致 |
| G3 | 脚本化对象 | `ENT`（实体）、`SWEP`（武器）、`GM`（游戏模式）、可选 `NPC`，用 `scripted_ents` / `weapons` 注册 |
| G4 | 不破坏原版 | 原版 HL1 地图、模型、音效、存档（尽力）继续可用；没有 Lua 脚本时行为与原版一致 |
| G5 | 热重载 | 保存脚本即生效，`hook.Add` 的标识符机制保证重载不重复注册 |
| G6 | 跨平台 | Xash3D FWGS 支持的平台尽量都能跑：Windows / Linux / macOS / Android（x86、ARM）；不支持 JIT 的平台降级 |
| G7 | 默认安全 | 客户端下载服务端下发的 Lua，必须沙箱化 |

### 1.2 非目标（v1 不做）

- **不做**完整 GMod API 兼容（"精神兼容"，不是"二进制兼容"）。GMod 的物理（`PhysObj`）、`Derma` 全套、`Workshop`、`Lua 版 NextBot` 等在 HL1 上没有对应物。
- **不做**Lua 预测移动（`SetupMove` / `Move` 在客户端预测）——风险极高，放到 v2 之后。
- **不做**脚本热更新引擎本体。
- **不做**脚本字节码分发（只分发文本源码，便于审计，见第 13 章）。

---

## 2. 总体架构决策

### 2.1 三种集成方案对比

| 方案 | 做法 | 优点 | 缺点 |
|------|------|------|------|
| **A. 集成进 mod 的 gamedll / client dll（推荐）** | 基于 `hlsdk-portable` 派生一份 mod 源码，新增 `lua/` 模块，同时编进服务端 DLL 与客户端 DLL | 不改引擎；所有 Xash3D 平台共用；符合 GoldSrc 的"mod 即游戏逻辑"传统；Android 打包简单 | 需要使用你自己编译的 `hl.dll` / `client.dll`，原版 Valve DLL 不能直接用 |
| B. 修改 xash3d-fwgs 引擎 | 在引擎里嵌 Lua，通过新增 API 暴露给 DLL | 能拿到更多内部数据（网络、渲染） | 需要维护引擎分支；用户必须用你的引擎；与上游合并成本高 |
| C. Metamod 插件 | 做成 Metamod 模块 | 能给现有服务器加脚本 | 仅服务端；客户端 HUD/渲染做不了；HL1 版 Metamod 在 Xash 上兼容性需评估 |

**基线 SDK 选择：`FWGS/hlsdk-portable`。** 它是 Xash3D 生态事实上的标准 SDK：同时提供 CMake 与 waf 构建，自带 `android/` Gradle 工程，并有面向 iOS、Switch 等平台的构建说明。Featureful SDK（基于 hlsdk-portable，自带自定义 `delta.lst`）可借鉴其网络字段设计；Half-Life Unified SDK 功能强，但面向 Steam 版 GoldSrc / 桌面平台，不适合作为多平台基线。完整的仓库结构、需要改动的 SDK 位置和构建矩阵见配套文件 `docs/INTEGRATION.md`（随项目骨架 zip 提供）。

**结论：采用方案 A。** 引擎保持原样，所有 Lua 逻辑以"mod 侧库"形式存在。若后续发现某些能力（如字体渲染、任意网络包）必须由引擎提供，再以**最小补丁 + 向上游提 PR** 的方式处理，而不是 fork 长期分叉。

### 2.2 模块分层

```
┌───────────────────────────────────────────────────────────────┐
│                       Lua 脚本层（GLua 风格）                   │
│   gamemodes/*   lua/autorun/*   lua/entities/*   lua/weapons/*  │
├───────────────────────────────────────────────────────────────┤
│                  Lua 核心层（includes/，随 mod 发行）            │
│   init.lua  hook.lua  timer.lua  net.lua  scripted_ents.lua ... │
│   —— 纯 Lua 实现，能用 Lua 写的绝不用 C++ 写                     │
├───────────────────────────────────────────────────────────────┤
│                 绑定层（C++，lua_api_*.cpp）                    │
│   Entity / Player / Vector / Angle / util / engine / surface    │
│   热路径用 LuaJIT FFI 直连 C 函数表；其余用 lua_CFunction        │
├───────────────────────────────────────────────────────────────┤
│                 运行时（C++，lua_runtime.cpp）                  │
│   VM 生命周期 / 文件系统 VFS / 错误处理 / 热重载 / 沙箱          │
├──────────────────────────────┬────────────────────────────────┤
│   服务端 DLL（dlls/）         │   客户端 DLL（cl_dll/）          │
│   DLL_FUNCTIONS / CBaseEntity │   HUD_* / V_CalcRefdef / TriAPI │
├──────────────────────────────┴────────────────────────────────┤
│                       Xash3D FWGS 引擎                          │
└───────────────────────────────────────────────────────────────┘
```

**设计原则**

1. **薄 C++，厚 Lua**：C++ 只负责"把引擎能力安全地露出来"，GMod 风格的库（`hook`、`timer`、`scripted_ents`、`net` 的高层 API 等）用 Lua 写在 `lua/includes/` 里。这样迭代快，也方便社区贡献。
2. **共享代码**：`common/lua/` 放 VM 管理、Vector/Angle、文件系统、沙箱这些服务端/客户端共用的 C++ 代码，通过编译宏 `XGLUA_SERVER` / `XGLUA_CLIENT` 区分 realm。
3. **一个 realm 一个 VM**：服务端一个 `lua_State`，客户端一个 `lua_State`，互不共享。跨 realm 通信只走 `net`。
4. **零侵入原版逻辑**：所有 Lua 回调点都先判断 VM 是否存在 / hook 是否有注册，没有则直接走原版 C++ 逻辑。

---

## 3. 目录结构与文件约定

### 3.1 游戏目录（运行时）

沿用 GMod 的目录命名，用户零学习成本：

```
<gamedir>/                         例如 valve/ 或 mymod/
├── dlls/hl.dll                    ← 带 XGLua 的服务端库
├── cl_dlls/client.dll             ← 带 XGLua 的客户端库
├── lua/
│   ├── includes/                  ← 核心 Lua 库（随 mod 发行，只读）
│   │   ├── init.lua               ← VM 启动后第一个被执行的文件
│   │   ├── enum.lua               ← 常量：MOVETYPE_*、SOLID_*、DMG_* ...
│   │   ├── extensions/            ← string.* table.* math.* 扩展
│   │   └── modules/               ← hook.lua timer.lua net.lua ...
│   ├── autorun/                   ← 双端都自动执行
│   │   ├── server/                ← 仅服务端自动执行
│   │   └── client/                ← 仅客户端自动执行
│   ├── entities/                  ← 脚本化实体
│   │   ├── my_ent.lua             ← 单文件形式：ENT 表
│   │   └── my_ent/                ← 目录形式：init.lua / cl_init.lua / shared.lua
│   ├── weapons/                   ← 脚本化武器（SWEP）
│   └── effects/                   ← 客户端特效（可选）
├── gamemodes/
│   ├── base/                      ← 基础游戏模式（随 mod 发行）
│   │   ├── base.txt               ← 元数据
│   │   └── gamemode/
│   │       ├── init.lua           ← 服务端入口
│   │       ├── cl_init.lua        ← 客户端入口
│   │       └── shared.lua         ← 共享
│   └── mygamemode/                ← 用户游戏模式（base = "base"）
├── data/                          ← file.Read/Write 沙箱根目录
└── cache/lua/<server_id>/         ← 客户端缓存的服务端 Lua（可选，见 4.4）
```

### 3.2 游戏模式元数据 `gamemodes/<name>/<name>.txt`

保持 GMod 的 KeyValues 格式，C++ 里写个 50 行的小解析器即可：

```
"mygamemode"
{
    "base"      "base"
    "title"     "My Gamemode"
    "author"    "you"
    "menusystem" "0"
}
```

### 3.3 服务器配置

通过 `server.cfg` / `game.cfg` 的 cvar 选择游戏模式：

```
xgl_gamemode "mygamemode"      // 类似 GMod 的 gamemode 指令
```

### 3.4 源码目录（C++ 一侧）

基于 `hlsdk-portable` 新增：

```
common/lua/
  lua_runtime.{h,cpp}        VM 创建/销毁、pcall 封装、traceback、热重载
  lua_vfs.{h,cpp}            Lua 专用虚拟文件系统（读 gamedir，写 data/）
  lua_sandbox.{h,cpp}        环境表构建、危险库剔除
  lua_types.{h,cpp}          Vector / Angle / Color 的 FFI cdata 与元表
  lua_syntax.{h,cpp}         GLua 语法预处理（!= && || continue /* */）
  lua_netcodec.{h,cpp}       net 库的位流编码（共享）
dlls/lua/
  lua_server.cpp             服务端入口：GameDLLInit/ServerActivate 等挂接
  lua_api_entity.cpp         Entity / Player / Weapon 方法
  lua_api_ents.cpp           ents.* / scripted_ents 桥
  lua_api_util.cpp           util.TraceLine / game.* / engine.*
  lua_cbase_lua.{h,cpp}      CLuaEntity / CLuaWeapon / CLuaMonster（见第 9 章）
  lua_net_server.cpp         服务端 net 传输层
cl_dll/lua/
  lua_client.cpp             客户端入口：HUD_Init/HUD_Redraw 等挂接
  lua_api_surface.cpp        surface / draw / render / input
  lua_net_client.cpp         客户端 net 传输层
3rdparty/luajit/             git submodule（见第 5 章）
```

---

## 4. Realm 与加载流程

### 4.1 全局标志

| 全局 | 服务端 | 客户端 |
|------|:----:|:----:|
| `SERVER` | `true` | `false` |
| `CLIENT` | `false` | `true` |
| `GM` / `GAMEMODE` | 当前游戏模式表 | 同左（由下发的脚本构建） |
| `MENU_DLL` | `false` | `false`（v1 不做菜单 realm） |

### 4.2 服务端启动顺序

对应 GMod 的 `Initialize` → `InitPostEntity` 两阶段。

```
GameDLLInit()            ← DLL_FUNCTIONS::pfnGameInit
   │  创建 lua_State；注册 C++ 绑定；构建沙箱环境
   │  执行 lua/includes/init.lua（加载 enum、extensions、modules）
   ▼
ServerActivate()         ← pfnServerActivate（地图实体全部 Spawn 完毕之后）
   │  ⚠️ 若需要在地图实体 Spawn 之前介入，要在 pfnSpawn 前置点处理
   ├─ 读取 xgl_gamemode，递归加载 base 链
   │     for each gamemode in [base, ..., current]:
   │         include("gamemodes/<n>/gamemode/shared.lua")
   │         include("gamemodes/<n>/gamemode/init.lua")
   ├─ 执行 lua/autorun/*.lua  →  lua/autorun/server/*.lua
   ├─ 扫描并注册 lua/entities/*、lua/weapons/*
   ├─ hook.Run("Initialize")
   └─ hook.Run("InitPostEntity")
```

> **地图加载顺序提示**：GMod 是"先 autorun，再 gamemode"；这里为了让脚本化实体能被地图引用，建议顺序为 **includes → gamemode → autorun → entities/weapons 注册 → Initialize**。具体次序以 GMod 行为为准，最终确定时请对照 GMod wiki。

### 4.3 客户端启动顺序

客户端 VM **每次连接服务器新建，断线销毁**（避免状态泄漏，也避免上一台服务器的脚本影响下一台）。

```
HUD_Init()                ← 客户端 DLL 启动：只初始化 VM 运行时、注册绑定，不执行脚本
   ...
连接服务器 → 服务器下发 Lua 文件清单（见 4.4）
   ▼
HUD_VidInit()             ← 地图加载/视频重启时调用（⚠️ 确认调用时机与连接状态）
   ├─ 创建新的客户端 lua_State（若已存在则先销毁）
   ├─ 执行 includes/init.lua
   ├─ 按服务端下发的清单执行 gamemode/cl_init.lua、shared.lua
   ├─ 执行 autorun 与 autorun/client
   ├─ hook.Run("Initialize")
   └─ 等待第一帧实体同步完成 → hook.Run("InitPostEntity")
```

### 4.4 `AddCSLuaFile` 与 Lua 文件下发

**语义（与 GMod 一致）**

- 服务端脚本中调用 `AddCSLuaFile("path.lua")` → 把该文件加入"需下发给客户端"的清单。
- 无参数调用 `AddCSLuaFile()` → 把**当前文件**加入清单。
- 客户端 `include("path.lua")` 时，从服务端下发的文件里取，而不是本地磁盘（防止客户端本地篡改）。
- `lua/includes/`、`lua/autorun/`（含 client）下的文件由客户端本地提供（随 mod 发行），**不下发**。

**下发通道（二选一，推荐 B）**

| 方案 | 做法 | 评价 |
|------|------|------|
| A. 引擎资源下载 | `PRECACHE_GENERIC("lua/xxx.lua")`，让客户端走引擎自带的文件下载（含 HTTP fastdl） | 实现简单；但 ⚠️ 引擎对可下载扩展名/路径可能有限制，且文件会落盘 |
| **B. 自建流式通道（推荐）** | 连接后服务端把清单中的文件**压缩（zlib/LZ4）后分片**，经 `net` 底层通道发给客户端；客户端放在**内存 VFS**，可选缓存到 `cache/lua/<server_id>/` | 完全可控；可做 CRC 校验、速率限制、断线重发；不写磁盘也可 |

**客户端侧开关（cvar）**

| cvar | 默认 | 含义 |
|------|------|------|
| `xgl_cl_allowdownload` | `1` | 是否接受服务端下发的 Lua |
| `xgl_cl_cache` | `1` | 是否缓存下发的文件（按 CRC 命中） |
| `xgl_cl_whitelist` | 空 | 服务器地址白名单；非空时仅白名单服务器可下发 |

服务端 cvar：`xgl_sv_allowcslua`（客户端能否 `RunString`，默认 `0`）、`xgl_sv_filelimit`（单文件大小上限，默认 256 KB）、`xgl_sv_totallimit`（总量上限，默认 8 MB）。

### 4.5 `include` 路径解析

1. 先相对**调用者所在目录**查找；
2. 再相对 `lua/` 根目录查找；
3. 客户端 realm：先查服务端下发的内存 VFS，再查本地（仅限白名单目录）。

`require` 支持 `lua/modules/*.lua`；**不**支持 C 模块（`package.loadlib` 被剔除，见第 13 章）。

---

## 5. LuaJIT 集成与构建

### 5.1 版本策略

- LuaJIT 官方已不再打版本标签，主线是 **`v2.1` 滚动分支**，版本号形如 `2.1.<unix时间戳>`（例如发行版里的 `2.1.1794386141`）。
- 做法：`3rdparty/luajit` 作为 **git submodule，锁定到某个具体 commit**，每季度评估一次升级。**不要**用"跟随分支最新"。
- 备选：OpenResty 维护的 `luajit2` 分支（修复较勤，适合服务端长期运行）。建议 CI 里同时能用两个分支编译，出问题可切换。

### 5.2 静态链接

```
make BUILDMODE=static            # Linux / macOS
msvcbuild.bat static             # Windows（x64 / x86 分别在对应的 VS 命令行下）
```

静态链进 `hl.dll` / `client.dll`，不额外发行 `lua51.dll`，避免版本冲突与符号污染。

### 5.3 平台矩阵

| 平台 | LuaJIT 状态 | 方案 |
|------|------------|------|
| Windows x86 / x64 | ✅ JIT | 默认 |
| Linux x86 / x64 / ARM / ARM64 | ✅ JIT | 默认 |
| macOS x64 / ARM64 | ✅ JIT | 设置 `MACOSX_DEPLOYMENT_TARGET` |
| Android ARMv7 / ARM64 / x86 / x86_64 | ✅ JIT | 用 NDK 交叉编译（宿主与目标位宽需一致，32 位目标需 32 位宿主工具或 `-m32` 的 host cc） |
| iOS | ⚠️ 系统禁止 JIT | 解释器模式运行（`-DLUAJIT_DISABLE_JIT`），性能下降但可用 |
| Nintendo Switch / PS Vita / 3DS / Wii / Xbox（Xash3D 的各移植分支） | ⚠️ 逐平台评估（多数没有可执行内存页，JIT 不可用） | 默认走 **PUC Lua 5.1** 后备；若平台允许再试 LuaJIT 解释器模式 |
| E2K / RISC-V / 其他 | ❌ LuaJIT 不支持 | **降级到 PUC-Rio Lua 5.1** + `bit` 库兼容层，FFI 路径全部走 `lua_CFunction` 后备实现 |

> Xash3D FWGS 本身支持不少冷门架构，所以 **"绑定层必须同时有 FFI 版和 lua_CFunction 版"** 这条要在 M1 阶段就定好，否则后面补成本很高。

### 5.4 编译选项建议

| 选项 | 建议 | 原因 |
|------|------|------|
| `LUAJIT_ENABLE_GC64` | x86_64 **开启** | 默认 x64 的 LuaJIT 对象需在低 2GB 地址内，宿主进程地址空间紧张时易出现 `not enough memory`；GC64 解除此限制（ARM64 默认已是 GC64） |
| `LUAJIT_ENABLE_LUA52COMPAT` | **开启** | 带来 `goto`（本来 LuaJIT 就有）、`__pairs`、`__len` 等 5.2 扩展，对 GLua 兼容有用 |
| `LUAJIT_DISABLE_FFI` | **不开启**，但脚本沙箱里**移除 `ffi`** | 核心层与绑定层要用 FFI 提速；见 13.2 |
| `LUAJIT_USE_SYSMALLOC` | 关闭 | 使用 LuaJIT 自带分配器 |
| 自定义分配器 | 使用 `lua_newstate(alloc, ud)` | 便于统计每个 VM 的内存、设置内存上限（如服务端 256 MB、客户端 128 MB） |

### 5.5 JIT 注意事项

- 热路径（`Think`、`HUDPaint`、`PlayerTick`）里避免 NYI（Not Yet Implemented）的函数，如 `pairs()`（在循环里）、`string.format` 的部分格式符等；在开发文档里给出 **JIT 友好编码指南**。
- 提供 `xgl_jit_status` / `xgl_jit_dump`（开发模式）命令，封装 `jit.status()` 与 `jit.v`、`jit.dump`。
- 绑定层中，**每次回调进 Lua 的 C→Lua 调用**不要频繁走 `ffi.cast` 回调（性能差，且会破坏 trace）；统一用 `lua_pcall` 调用 Lua 函数。

---

## 6. 数据类型与绑定层

### 6.1 Vector / Angle / Color

- 用 **LuaJIT FFI cdata + `ffi.metatype`** 实现，布局与引擎一致（3 个 `float`），可直接传给 C 函数、无需拷贝；运算符重载走 `__add`、`__sub`、`__mul`、`__eq`、`__tostring`；方法 `Length`、`Normalize`、`Dot`、`Cross`、`Distance`、`Angle` 等与 GMod 一致。
- 核心层 `lua/includes/` 使用 FFI 定义；因为沙箱会移除全局 `ffi`，需要在**沙箱构建之前**完成这些 metatype 的定义。
- 后备实现（无 FFI 平台）：纯 Lua table `{x=,y=,z=}` + 元表，API 完全一致。

```lua
local v = Vector(1, 2, 3)
v:Normalize()
local ang = v:Angle()
print(v, v:Length(), v + Vector(0, 0, 8))
```

### 6.2 Entity / Player / Weapon 句柄

不要把 `edict_t*` 直接给 Lua（实体被删除后会悬挂）。句柄用一个轻量对象：

```
struct EntHandle { int index; int serial; };   // 对应 edict_t::serialnumber
```

- 每次访问都校验 `index` 对应 edict 当前 `serialnumber == serial` 且 `!free`；否则当作 `NULL` 实体。
- `IsValid(ent)` 就是这个校验；`NULL` 实体调用任何方法抛出明确错误（GMod 风格：`Tried to use a NULL entity!`）。
- 元表继承：`Entity` → `Player` / `Weapon` / `NPC`；`ent:IsPlayer()` 判断后其元表为 `Player`。
- `Entity(index)` 全局函数按索引取实体；`ents.GetAll()` 返回当前存活实体。

### 6.3 绑定方式分级

| 类别 | 方式 | 例子 |
|------|------|------|
| 热路径（每帧大量调用） | FFI 直连 C 函数表 `xgl_api_t`（C 导出一张函数指针表，Lua 里 `ffi.cast` 一次） | `Entity:GetPos`、`GetVelocity`、`util.TraceLine`、向量运算 |
| 普通 API | `lua_CFunction` | `Entity:SetModel`、`Player:Give`、`ents.Create` |
| 复杂对象构造 | Lua 层封装 | `scripted_ents`、`weapons`、`net` 高层 API |

### 6.4 单位、原点与碰撞盒（与 GMod 的关键差异）

Vector / Angle 的含义与 GMod 一致（`Angle` 为 pitch, yaw, roll）。**但碰撞盒与原点约定不同，移植 GMod 脚本最容易在这里出错：**

| 项目 | HL1 / GoldSrc | GMod / Source |
|------|---------------|---------------|
| 玩家站立 hull | `(-16,-16,-36)` ~ `(16,16,36)`，32×32×72 | `(-16,-16,0)` ~ `(16,16,72)`，高度同为 72 |
| 玩家蹲伏 hull | `(-16,-16,-18)` ~ `(16,16,18)`，32×32×36 | 高度 36 |
| **实体原点** | **在 hull 中心**（站立时原点离脚底 36 单位） | **在脚底** |
| 世界碰撞 | 预编译的 clip hull（见第 19 章），不是任意 AABB | 任意 OBB / 物理对象 |

设计决策：

1. `Entity:GetPos()` 默认返回引擎原始 `pev->origin`（hull 中心），**不做隐式换算**，避免行为不一致；
2. 另提供 `Entity:GetFeetPos()`（脚底 Z = `origin.z + mins.z`）、`Entity:WorldSpaceCenter()`、`Entity:OBBMins()/OBBMaxs()`，文档里显著标注"GMod 脚本里 `GetPos()+Vector(0,0,N)` 要重新计算"；
3. 提供兼容开关 `xgl_gmod_origin 1`（默认 0）：打开后对 **Player** 的 `GetPos/SetPos` 使用脚底原点，仅用于快速移植旧脚本；
4. `util.TraceHull` 的 hull 参数用常量 `HULL_POINT / HULL_HUMAN / HULL_LARGE / HULL_HEAD`（对应 0/1/2/3），**不接受任意尺寸**——这是 BSP 编译期决定的；实体之间的碰撞（`SetCollisionBounds`）则是 AABB，可任意设置。

---

## 7. 核心库 API 清单

> 标记：🟢 = M1/M2 必做；🟡 = M3/M4；🔵 = 后续。

### 7.1 全局函数

| 函数 | 说明 | 优先级 |
|------|------|--------|
| `print` / `Msg` / `MsgN` / `MsgC` / `PrintTable` | 输出到控制台 | 🟢 |
| `Error` / `ErrorNoHalt` / `assert` | 错误与警告 | 🟢 |
| `include` / `AddCSLuaFile` / `require` | 文件加载 | 🟢 |
| `CurTime` / `RealTime` / `SysTime` / `FrameTime` / `UnPredictedCurTime` | 时间 | 🟢 |
| `Vector` / `Angle` / `Color` / `Entity` | 类型构造 | 🟢 |
| `IsValid` / `isstring` / `isnumber` / `istable` / `isfunction` / `isentity` / `isvector` ... | 类型判断 | 🟢 |
| `RunConsoleCommand` / `GetConVar` / `CreateConVar` / `CreateClientConVar` | 控制台 | 🟢 |
| `RunString` / `CompileString` / `CompileFile` | 动态执行（带权限） | 🟡 |
| `Material` / `Model` / `Sound` / `Sprite` | 资源句柄 | 🟡 |
| `LocalPlayer`（客户端）| 本地玩家 | 🟢 |
| `ScrW` / `ScrH` / `CurTime` / `ChatPrint`（Player 方法）| HUD 辅助 | 🟡 |

### 7.2 标准库扩展

与 GMod 一致地扩充 `string`、`table`、`math`：

- `string.Explode / Split / Trim / StartWith / EndsWith / Replace / FormattedTime / ToColor ...`
- `table.Copy / Merge / HasValue / Count / Random / GetKeys / Reverse / Empty / IsSequential ...`
- `math.Clamp / Round / Approach / Remap / Rand / NormalizeAngle / AngleDifference / Lerp / EaseInOut ...`
- `bit`：LuaJIT 自带，GMod 同款 API，直接可用。

### 7.3 服务端 / 客户端库

| 库 | 主要函数 | Realm | 优先级 |
|----|----------|:----:|:----:|
| `hook` | `Add` `Remove` `Run` `Call` `GetTable` | 共享 | 🟢 |
| `timer` | `Simple` `Create` `Remove` `Exists` `Adjust` `Pause` `UnPause` `Toggle` | 共享 | 🟢 |
| `concommand` | `Add` `Remove` | 共享 | 🟢 |
| `cvars` | `AddChangeCallback` `GetConVarCallbacks` | 共享 | 🟡 |
| `net` | `Start` `Write*` `Send` `Broadcast` `SendToServer` `Receive` `Read*` | 共享 | 🟢 |
| `ents` | `Create` `GetAll` `FindByClass` `FindByName` `FindInSphere` `FindInBox` `Iterator` | 服务端为主 | 🟢 |
| `player` | `GetAll` `GetByID` `GetBySteamID` `GetHumans` `GetBots` | 共享 | 🟢 |
| `team` | `SetUp` `GetPlayers` `GetScore` `GetName` `GetColor` | 共享 | 🟡 |
| `util` | `TraceLine` `TraceHull` `PrecacheModel` `PrecacheSound` `Effect` `ScreenShake` `BlastDamage` `PointContents` `IsInWorld` `Decal` `TableToJSON` `JSONToTable` `Base64Encode` `CRC` | 共享 | 🟢 |
| `game` | `GetMap` `ConsoleCommand` `MaxPlayers` `CleanUpMap` `LoadNextMap` `GetWorld` | 共享 | 🟡 |
| `engine` | `ActiveGamemode` `GetGames` | 共享 | 🔵 |
| `file` | `Read` `Write` `Append` `Exists` `Find` `Delete` `IsDir` `CreateDir` `Size` | 共享 | 🟡 |
| `scripted_ents` | `Register` `Get` `GetStored` `GetList` | 共享 | 🟢 |
| `weapons` | `Register` `Get` `GetList` | 共享 | 🟢 |
| `gamemode` | `Register` `Get` | 共享 | 🟢 |
| `sound` | `Add` `Play` `PlayFile` | 共享 | 🟡 |
| `surface` / `draw` / `render` / `input` / `cam` | 见第 11 章 | 客户端 | 🟡 |
| `vgui` / `Derma` | 轻量面板系统 | 客户端 | 🔵 |
| `http` | `Fetch` `Post` | — | 🔵（默认禁用） |

### 7.4 Entity 方法（子集，v1）

```
-- 位置与运动
GetPos SetPos GetAngles SetAngles GetVelocity SetVelocity GetAbsVelocity
GetMoveType SetMoveType GetSolid SetSolid SetCollisionBounds GetCollisionBounds
GetGroundEntity OnGround WaterLevel

-- 模型与渲染
GetModel SetModel SetSkin GetSkin SetBodygroup GetBodygroup SetSequence GetSequence
SetRenderMode SetRenderFX SetColor GetColor SetSprite? ResetSequence SequenceDuration

-- 属性
EntIndex GetClass GetName SetName GetOwner SetOwner GetParent SetParent
Health SetHealth GetMaxHealth SetMaxHealth

-- 行为
Spawn Activate Remove Fire Input Use Touch SetNextThink NextThink
EmitSound StopSound TakeDamage TakeDamageInfo
SetTouch SetUse SetThink      -- 对应 HL1 的 SetTouch/SetThink 函数指针

-- 键值 / 网络变量
SetKeyValue GetKeyValues NetworkVar SetNW* GetNW* SetNWVarProxy

-- 关系
GetEnemy SetEnemy Visible? FindEntity?    -- 视是否做 NPC 而定
```

### 7.5 Player 方法（子集，v1）

```
Nick Name SteamID UserID Ping Team SetTeam Frags SetFrags Deaths SetDeaths
Alive Kill KillSilent Spawn Give GiveAmmo StripWeapons StripWeapon HasWeapon
GetActiveWeapon GetWeapons SelectWeapon SetActiveWeapon
Armor SetArmor GetAimVector EyePos EyeAngles ShootPos
GetEyeTrace KeyDown KeyPressed KeyReleased IsOnGround Crouching InVehicle
ChatPrint PrintMessage SendLua ConCommand SetFOV ScreenFade ViewPunch
IsBot IsAdmin IsSuperAdmin IsConnected
```

> **SteamID 说明**：Xash3D 没有真实 Steam 认证（⚠️ 视版本而定）；`SteamID()` 返回引擎提供的 AuthID（通常是 `STEAM_ID_LAN` / `VALVE_ID_LAN` 等），**不能**作为可信身份。需要持久身份的脚本，建议由 XGLua 额外提供 `player:UniqueID()`（基于 userinfo 中的稳定 key，如 `cl_` 自定义 cvar 或服务端签发 token）。

---

## 8. Hook 映射表

### 8.1 服务端：GMod Hook ←→ HLSDK 入口

| GMod Hook | 参数 | 触发源（HLSDK） | 说明 |
|-----------|------|-----------------|------|
| `Initialize` | — | `ServerActivate`（见 4.2） | |
| `InitPostEntity` | — | 同上，实体都已就绪 | |
| `ShutDown` | — | `ServerDeactivate` | |
| `Think` | — | `StartFrame` | 每个服务器帧 |
| `Tick` | — | `StartFrame`（固定 tick 节流） | ⚠️ HL1 无固定 tick，建议按 `xgl_tickrate` 软件节流 |
| `PlayerConnect` / `CheckPassword` | name, addr / ... | `ClientConnect` | 返回 `false, reason` 拒绝 |
| `PlayerInitialSpawn` | ply | `ClientPutInServer` | |
| `PlayerSpawn` | ply | `CBasePlayer::Spawn` | |
| `PlayerDisconnected` | ply | `ClientDisconnect` | |
| `PlayerSay` | ply, text, teamonly | `ClientCommand("say"/"say_team")` | 返回字符串替换内容，返回 `""` 吞掉 |
| `PlayerDeath` | victim, inflictor, attacker | `CBasePlayer::Killed` | |
| `DoPlayerDeath` | ply, attacker, dmginfo | 同上（早于 `PlayerDeath`） | |
| `PlayerShouldTakeDamage` | ply, attacker | `CBasePlayer::TakeDamage` / `CGameRules::FPlayerCanTakeDamage` ⚠️ 待确定统一接入点 | 返回 `false` 免伤；需验证单人和无模式行为以及重复派发 |
| `EntityTakeDamage` | ent, dmginfo | ⚠️ 具体接入点和覆盖范围待验证 | 可改 `dmginfo` 或返回 `true` 阻止；多个实体重写 `TakeDamage`，不能仅在基类接线后宣称覆盖所有实体 |
| `PlayerCanPickupWeapon` | ply, wep | `CBasePlayerItem::AddToPlayer` 前 | |
| `PlayerCanPickupItem` | ply, item | 物品 `Touch` | |
| `PlayerSwitchWeapon` | ply, old, new | `SelectItem` | |
| `PlayerLoadout` | ply | `CBasePlayer::Spawn` 末尾 | 默认装备 |
| `PlayerSetModel` | ply | `Spawn` | |
| `PlayerTick` | ply, mv? | `PlayerPreThink` / `PlayerPostThink` | v1 不暴露 `CMoveData` |
| `KeyPress` / `KeyRelease` | ply, key | `CmdStart`（比对 buttons 变化） | 需要保存上一帧 buttons |
| `EntityKeyValue` | ent, key, value | `KeyValue` | 地图实体键值拦截 |
| `OnEntityCreated` | ent | `Spawn` 前后 | |
| `EntityRemoved` | ent | `UpdateOnRemove` | |
| `CanPlayerSuicide` | ply | `ClientKill` | |
| `SetupPlayerVisibility` | ply, viewent | `SetupVisibility` | 用于添加额外 PVS |
| `ShouldCollide` | ent1, ent2 | `pfnShouldCollide`（⚠️ 若引擎导出）| |
| `AllowPlayerPickup` / `GravGunPickupAllowed` | — | — | 无对应物，**不实现** |

#### 8.1.1 游戏模式 ↔ `CGameRules`（HL1 的规则层）

HL1 的 DM/SP 规则放在 `CGameRules` 的子类里（`CHalfLifeRules` 单人、`CHalfLifeMultiplay` 多人），由 `InstallGameRules()` 创建。**Lua 游戏模式的正确落点是子类化 `CHalfLifeMultiplay` 得到 `CLuaGameRules`，把虚函数转发给 `GM`**，而不是到处改 `CBasePlayer`。下表中的函数名来自 HLSDK 的 `gamerules.h`；标 ⚠️ 的为凭记忆列出，需对照实际头文件确认。

> **实现核验待办：** 此分支的 `InstallGameRules()` 还会选择 teamplay 和 busters 规则。`CLuaGameRules` 的适用范围、单人模式及与 8.1 已有 hook 的派发次序和去重均需先核对；`CGameRules` 不是所有实体伤害的统一入口。Lua 回调可同步删除实体，相关调用链的生存期安全必须先解决。

| `GM:` 回调 | 对应 `CGameRules` 虚函数 | 说明 |
|-----------|--------------------------|------|
| `PlayerSpawn(ply)` | `PlayerSpawn` | 玩家出生前最后一步 |
| `PlayerShouldTakeDamage(ply, attacker)` | `FPlayerCanTakeDamage` | 返回 `false` 免伤 |
| `PlayerSelectSpawn(ply)` | `GetPlayerSpawnSpot` | 返回出生点实体 |
| `PlayerDeath(victim, inflictor, attacker)` | `PlayerKilled` | 每次玩家死亡 |
| `GetPointsForKill(attacker, victim)` | `IPointsForKill` | HL 特有，GMod 无对应 |
| `AllowMonsters()` | `FAllowMonsters` | |
| `PlayerSwitchWeapon(ply, old, new)` | `FShouldSwitchWeapon` | |
| `GetNextBestWeapon(ply, cur)` | `GetNextBestWeapon` | 武器用完后的自动切换 |
| `GetFallDamage(ply)` | `FlPlayerFallDamage` | 返回伤害值 |
| `ShouldAutoAim(ply, target)` | `ShouldAutoAim` | |
| `Think()` | `CHalfLifeMultiplay::Think` | 每帧规则逻辑（换图、倒计时） |
| `PlayerDisconnected(ply)` | `ClientDisconnected` | |
| `PlayerCanPickupWeapon / Item` | `CanHavePlayerItem` ⚠️ 等 | |
| `PlayerCanRespawn / PlayerSpawnTime` | `FPlayerCanRespawn` / `FlPlayerSpawnTime` ⚠️ | |

> `base` 游戏模式 = 不覆盖任何回调时，`CLuaGameRules` 直接调用 `CHalfLifeMultiplay` 的原版实现，从而保证"装上 XGLua 不写脚本"与原版 DM 行为一致。

### 8.2 客户端：GMod Hook ←→ 客户端 DLL 入口

| GMod Hook | 触发源（`cl_dll`） | 说明 |
|-----------|--------------------|------|
| `Initialize` / `InitPostEntity` | 见 4.3 | |
| `Think` | `HUD_Frame` | 每个渲染帧 |
| `HUDPaint` | `HUD_Redraw` | 2D 绘制 |
| `HUDPaintBackground` | `HUD_Redraw` 开头 | |
| `HUDShouldDraw(name)` | 各 HUD 元素绘制前 | 返回 `false` 隐藏原版 HUD 元素（health/ammo/crosshair...） |
| `CalcView(ply, pos, ang, fov)` | `V_CalcRefdef` | 返回 `{origin, angles, fov}` |
| `CalcViewModelView` | `V_CalcRefdef`（view model 部分） | |
| `PreDrawOpaqueRenderables` / `PostDrawOpaqueRenderables` | `HUD_DrawNormalTriangles` | 3D 绘制（TriAPI） |
| `PostDrawTranslucentRenderables` | `HUD_DrawTransparentTriangles` | |
| `PostDrawViewModel` | `HUD_DrawTransparentTriangles` 附近 | |
| `CreateMove(cmd)` | `CL_CreateMove` | 修改 `cmd` 视角/按钮 |
| `PlayerBindPress(ply, bind, pressed)` | `HUD_Key_Event` | 返回 `true` 拦截 |
| `KeyPress` | `HUD_Key_Event` / `CL_CreateMove` | |
| `OnPlayerChat` | `pfnTextMsg` / SayText 消息 | |
| `RenderScreenspaceEffects` | 后处理阶段 | ⚠️ 取决于渲染器（软件/GL/Vk）能否提供，**v1 不做** |
| `NetworkEntityCreated` | `HUD_AddEntity` 首次见到 | |
| `OnReloaded` | 热重载后 | |

### 8.3 Hook 库行为（与 GMod 一致）

```lua
hook.Add("Think", "my_think", function() ... end)   -- (事件名, 唯一标识, 函数)
hook.Remove("Think", "my_think")
local r = hook.Run("PlayerSay", ply, text, false)   -- 先调用 GAMEMODE:PlayerSay，再依次调用注册的 hook
```

规则：

1. 事件触发时，先遍历 `hook.Add` 注册表（顺序按标识字符串排序，保证确定性——GMod 同样不保证顺序，我们给出确定顺序更利于调试），**任何返回值非 `nil` 即终止并作为结果**；
2. 若无 hook 返回，再调用 `GAMEMODE[name]`（`hook.Run` 才调；`hook.Call` 可以指定 gamemode 表）；
3. 标识符可以是字符串或带 `IsValid` 的对象（对象失效时自动移除，GMod 的同名机制）；
4. **热点事件优化**：C++ 侧缓存"该事件是否有任何监听者"，没有就**不进 Lua**（`Think`、`HUDPaint` 等）。

---

## 9. 脚本化实体 / 武器 / NPC / 游戏模式

### 9.1 scripted_ents：Lua 实体怎么落到 HL1 实体系统里

**难点**：HL1 引擎按 `classname` 创建实体时，是**在游戏 DLL 的导出符号表里按名字查找函数**（`LINK_ENTITY_TO_CLASS` 生成的导出函数）。Lua 实体的类名是**运行时才知道**的，无法生成导出符号。⚠️（需确认 Xash3D 的 `pfnCreateNamedEntity` / 地图解析路径；下为假设）

**方案**

1. **C++ 里只定义一个通用类** `CLuaEntity : CBaseAnimating`（继承链按 `ENT.Type` 选择 `anim` / `point` / `brush` 等不同基类，可有 `CLuaAnimEntity`、`CLuaPointEntity`、`CLuaBrushEntity` 三个）。
2. **`ents.Create("my_ent")` 的路径（脚本创建，主流程）**：
   - 调用 `CREATE_ENTITY()` 拿到裸 edict（**不**经过名字查找）；
   - 用 placement new 在 `pvPrivateData` 上构造 `CLuaEntity`，设置 `pev->classname = "my_ent"`，并让其持有 Lua 侧的 `ENT` 类表引用；
   - 调用 Lua `ENT:Initialize()`。
3. **地图里直接放 Lua 实体（Hammer 放置）**：
   - 提供 **通用导出实体** `lua_entity`（`LINK_ENTITY_TO_CLASS(lua_entity, CLuaEntity)`），地图里用 `classname lua_entity` + 键值 `luaclass my_ent`；
   - 或者，提供 **FGD 生成工具**，自动从 `lua/entities/*` 生成 FGD，让 Hammer 里能直接选；
   - 或者，预置 **`.ent` 覆盖**（⚠️ Xash3D 支持 `maps/<map>.ent` 覆盖实体 lump；可在加载前把 `classname my_ent` 改写成 `lua_entity + luaclass`）；
   - 备选：预留 N 个导出符号 `lua_ent_01 ~ lua_ent_64`，运行时动态映射——不推荐，污染命名空间。
4. **存档/读档**：Lua 实体用 `luaclass` 字段 + `ENT:Save/Restore` 序列化为字符串，写入 `SAVERESTOREDATA`（✅ v1 可以先**不支持**存档，文档里标明）。

### 9.2 ENT 表格式（与 GMod 一致）

```lua
-- lua/entities/xgl_medkit/shared.lua（或单文件 lua/entities/xgl_medkit.lua）
ENT.Type      = "anim"            -- anim | point | brush
ENT.Base      = "base_entity"     -- 可继承其他 Lua 实体
ENT.PrintName = "Medkit"
ENT.Author    = "you"
ENT.Spawnable = true

function ENT:SetupDataTables()
    self:NetworkVar("Int", 0, "Charge")          -- 同步给客户端的变量
end
```

```lua
-- init.lua（仅服务端）
AddCSLuaFile("shared.lua")
include("shared.lua")

function ENT:Initialize()
    self:SetModel("models/w_medkit.mdl")
    self:SetMoveType(MOVETYPE_TOSS)
    self:SetSolid(SOLID_TRIGGER)
    self:SetCollisionBounds(Vector(-8, -8, 0), Vector(8, 8, 16))
    self:SetCharge(25)
end

function ENT:Touch(other)
    if IsValid(other) and other:IsPlayer() and other:Health() < other:GetMaxHealth() then
        other:SetHealth(math.min(other:GetMaxHealth(), other:Health() + self:GetCharge()))
        self:EmitSound("items/smallmedkit1.wav")
        self:Remove()
    end
end
```

```lua
-- cl_init.lua（仅客户端）
include("shared.lua")
function ENT:Draw() self:DrawModel() end
```

### 9.3 `ENT` 生命周期回调 ←→ HL1

| `ENT:` 回调 | HL1 对应 |
|-------------|----------|
| `Initialize` | `Spawn()` |
| `KeyValue(k, v)` | `KeyValue()` |
| `Think` | `Think()`（通过 `SetNextThink` 控制节奏） |
| `Touch(other)` | `Touch()` |
| `Use(activator, caller, useType, value)` | `Use()` |
| `OnTakeDamage(dmginfo)` | `TakeDamage()` |
| `OnRemove` | 析构 / `UpdateOnRemove` |
| `StartTouch` / `EndTouch` | 由 C++ 侧对 `Touch` 做进出差分（⚠️ 可选） |
| `Draw` / `DrawTranslucent`（客户端）| `HUD_AddEntity` / `HUD_DrawNormalTriangles` |

### 9.4 `NetworkVar`：实体变量同步

GMod 的 `NetworkVar` 在 HL1 上没有现成通道。设计为**两级后端**：

| 后端 | 机制 | 容量 | 特点 |
|------|------|------|------|
| **快速槽** | 复用 `entity_state_t` 的 `iuser1~4` / `fuser1~4` / `vuser1~4`（⚠️ 先确认哪些已被原版 DLL 占用，如 `iuser3/4` 常被武器/玩家状态占用） | 约 12 个 | 随实体帧同步，自带 PVS 与增量压缩，延迟最低 |
| **溢出通道** | 超出的变量走 `net` 底层通道，由服务端对每个实体维护脏位，变更时向 PVS 内玩家发包 | 无硬限制（受带宽约束） | 稍慢，但通用 |

> **关于"快速槽"的现实约束**（来自 TWHL 教程与 SDK 源码的观察）：原版 SDK 已经占用了一部分 `iuser/fuser/vuser` 字段——例如自定义弹药教程把额外弹药塞进 `clientdata_t` 的 `vuser2/vuser3`，"添加用户变量"教程把 `iuser4` 用作"服务端更新"标志。动手前必须**逐字段盘点占用**；字段能否被网络同步以及占多少位，由 `delta.lst` 决定。mod 可以自带 `delta.lst`（Featureful SDK 就这么做，例如把 `clientdata_t/weapon_data_t` 的 `m_iId` 扩到 6 位以支持更多武器的预测），因此本项目**必须随 mod 发行自己的 `delta.lst`**，并提供 `tools/check_delta.py` 检查快速槽与 `delta.lst` 是否一致。

`NetworkVar` 的 `Int/Float/Bool/String/Vector/Angle/Entity` 类型映射到上述后端，并生成 `Get<Name>/Set<Name>`；`SetNW*/GetNW*`（按名字的动态变量）一律走溢出通道。

### 9.5 SWEP：脚本化武器（**风险最高的模块**）

HL1 的武器有**客户端预测**：`cl_dll/hl/hl_weapons.cpp` 里有一份与服务端几乎相同的武器逻辑，在客户端本地运行以隐藏延迟（`cl_lw 1`）。Lua 武器要做到"手感不输原版"，也得有预测。因此分两阶段：

| 阶段 | 做法 | 手感 | 难度 |
|------|------|------|------|
| **P1：服务端权威** | SWEP 只在服务端运行；视觉效果（视图模型动画、枪口火焰、声音）通过**服务端发起 `PLAYBACK_EVENT` 或 `net`** 通知客户端；该武器标记为 `lw = 0`（禁用客户端预测） | 有延迟（LAN/低 ping 下可接受） | 低 |
| **P2：共享预测** | 把 `SWEP:PrimaryAttack / SecondaryAttack / Reload / Think` 在**客户端预测环节**（`HUD_WeaponsPostThink` 一类）也执行一次，但**只允许调用"可预测 API 子集"**：`SendWeaponAnim`、`SetNextPrimaryFire`、`EmitSound`（本地）、`ViewPunch`、`ShootBullet`（客户端只播特效不造成伤害）等 | 接近原版 | 高，需严格限制 API |

**武器系统的硬约束（来自 TWHL「Weapons Programming」系列与 Featureful 文档）**

- 每把武器需要**唯一的武器 ID** 与唯一的 `slot + position` 组合；
- 武器 ID 要通过网络做客户端预测，默认 `delta.lst` 里 `m_iId` 位宽偏小（Featureful 把它扩到 6 位以支持更多武器）。所以 Lua 武器**不能"想要多少有多少"**：设计成**预留 ID 池**（如 `WEAPON_LUA_1..N`），`weapons.Register` 时分配，N 由 `delta.lst` 位宽决定；
- 客户端预测代码在 `cl_dll/hl/hl_weapons.cpp`：`HUD_WeaponsPostThink` 里按武器 ID 选择客户端本地武器对象。Lua 武器的 P2（预测）方案就是在这里放一个通用的 `CLuaWeaponClient` 并转发给客户端 Lua；
- 预测可以整体关闭：`cl_lw 0`，或编译时不定义 `CLIENT_WEAPONS`（把客户端逻辑退回服务端）——这正是 P1「服务端权威」方案的依据。

SWEP 表格式（与 GMod 一致）：

```lua
SWEP.PrintName    = "Plasma Pistol"
SWEP.Slot         = 1
SWEP.SlotPos      = 4
SWEP.ViewModel    = "models/v_9mmhandgun.mdl"
SWEP.WorldModel   = "models/w_9mmhandgun.mdl"
SWEP.PlayerModel  = "models/p_9mmhandgun.mdl"     -- HL1 特有：第三人称持握模型
SWEP.Primary.ClipSize    = 12
SWEP.Primary.DefaultClip = 24
SWEP.Primary.Ammo        = "9mm"
SWEP.Primary.Automatic   = false

function SWEP:PrimaryAttack()
    if not self:CanPrimaryAttack() then return end
    self:SetNextPrimaryFire(CurTime() + 0.2)
    self:TakePrimaryAmmo(1)
    self:ShootBullet(8, 1, Vector(0.01, 0.01, 0))
    self:SendWeaponAnim(WEAPON_ANIM_FIRE1)     -- HL1 用序列号，不是 ACT_VM_*
    self:EmitSound("weapons/pl_gun3.wav")
    self:GetOwner():ViewPunch(Angle(-2, 0, 0))
end
```

C++ 侧：`CLuaWeapon : CBasePlayerWeapon`，所有虚函数（`Deploy`、`Holster`、`PrimaryAttack`、`SecondaryAttack`、`Reload`、`WeaponIdle`、`ItemPostFrame`）转发到 Lua；`ItemInfo`（`GetItemInfo`）由 `SWEP.*` 字段填充。

> **HL1 特有差异**：动画是 **sequence 编号**（模型 QC 决定），没有 `ACT_VM_PRIMARYATTACK`；我们提供按模型名映射的常量表（`WEAPON_ANIM_*`）和 `self:SendWeaponAnim(seq)` 直接传序号。

### 9.6 Lua NPC（可选，v2）

`CLuaMonster : CBaseMonster`，将 `MonsterThink`、`Schedule`、`TakeDamage`、`Killed` 等转发给 Lua；先提供**简化 AI 接口**（`NPC:SetTarget` / `NPC:MoveTo` / `NPC:Attack`），不开放 HL1 完整 Schedule/Task 系统（过于复杂，容易写出崩溃）。

### 9.7 游戏模式 `GM`

```lua
-- gamemodes/mygamemode/gamemode/shared.lua
GM.Name   = "My Gamemode"
GM.Author = "you"
DeriveGamemode("base")           -- 继承 base

function GM:Initialize() end
```

```lua
-- gamemodes/mygamemode/gamemode/init.lua
AddCSLuaFile("cl_init.lua")
AddCSLuaFile("shared.lua")
include("shared.lua")

function GM:PlayerSpawn(ply)
    self.BaseClass.PlayerSpawn(self, ply)
    ply:Give("weapon_xgl_plasma")
end
```

`base` 游戏模式默认实现原版 HL1 deathmatch 的规则（计分、重生、武器掉落、换图投票），**使得"装上 XGLua 但不写脚本"的服务器行为与原版 HL DM 一致**。

---

## 10. 网络系统

### 10.1 `net` 库 API（与 GMod 一致）

```lua
-- 服务端
util.AddNetworkString("my_msg")                  -- 注册消息名（服务端必须先注册）
net.Start("my_msg")
    net.WriteString("hello")
    net.WriteUInt(42, 8)
    net.WriteVector(ply:GetPos())
    net.WriteEntity(ent)
net.Send(ply)                                    -- 或 net.Broadcast()、net.SendOmit(ply)、net.SendPVS(pos)

net.Receive("my_other_msg", function(len, ply)   -- 服务端收到客户端消息，ply 为发送者
    local s = net.ReadString()
end)

-- 客户端
net.Receive("my_msg", function(len)
    local s = net.ReadString()
    local n = net.ReadUInt(8)
end)
net.Start("my_other_msg"); net.WriteString("hi"); net.SendToServer()
```

支持的类型：`Bool Int(bits) UInt(bits) Float Double String Vector Angle Entity Color Data Table`（`Table` 走自定义序列化，限制深度与大小）。

### 10.2 底层传输（GoldSrc 限制很多，必须自己做一层）

| 方向 | 可用通道 | 限制 | 设计 |
|------|----------|------|------|
| 服务端→客户端 | **用户消息（User Message）** `svc_usermessage` | GoldSrc（协议 48）单条用户消息**最大 192 字节**（带字符串时有效载荷约 188）；消息名**不宜超过 12 字节**（TWHL 评论区的经验值，⚠️ 实测确认）；用户消息种类上限：GoldSrc 约 128，Xash3D 191 | **只注册一条** `XGLNet`（6 字节，满足长度限制）的变长用户消息，载荷头含 `msgid(uint16)` `flags` `seq` `fragindex`，自己做**分片与重组**。**分片大小运行时协商**：默认按 GoldSrc 保守值（≤ 160 字节有效载荷），检测到 Xash 协议 49 时可放大（旧版 Xash3D 更新日志称用户消息上限提升到 2048 字节，⚠️ 必须对 FWGS 当前版本实测后才能启用） |
| 客户端→服务端 | **字符串命令**（`pfnServerCmd` → `ClientCommand`） | 单条命令 ≤ 255 字符，且受引擎命令速率限制 | 将二进制载荷 **Base64/Base85 编码**，命令形如 `xgln <seq> <idx>/<total> <chunk>`；服务端组装 |
| 大块数据（Lua 文件下发） | 自建流 | — | 压缩 + 分片 + 确认窗口；按客户端维护**发送预算**，避免撑爆可靠通道（HL1 可靠缓冲溢出会直接把客户端踢出） |

**关键点**

1. **发送队列 + 令牌桶**：每个客户端一个队列，按带宽预算出队（`xgl_net_bps`，默认 8 KB/s 可调）；队列堆积超过上限（`xgl_net_queue_limit`）则丢弃低优先级消息并告警。
2. **可靠性**：用户消息本身是可靠通道（走 netchan 可靠流），因此 `net` 默认可靠；若要不可靠（高频、可丢），**另做** `net.SendUnreliable`（映射到不可靠的 `entity_state` 字段或 `pfnMessageBegin` 的 `MSG_ONE_UNRELIABLE`），v1 可不做。
3. **反作弊/防滥用**：客户端→服务端每秒最多 N 条 `net` 消息（`xgl_net_rate_limit`，默认 60 条/秒/玩家），超限丢弃并计数，超过阈值踢出。
4. **消息名映射**：`util.AddNetworkString` 在服务端生成 `name → uint16 id`，连接时下发整张表（算一次"字符串表同步"）；客户端 `net.Receive` 按名称查表。

### 10.3 `SendLua` / `BroadcastLua`

`ply:SendLua("code")` 在客户端执行代码字符串，**默认需客户端允许**（`xgl_cl_allowsendlua`，默认 `0`；GMod 默认开启，但 HL1 生态更"小作坊"，默认关更稳妥）。

---

## 11. 客户端渲染与 UI

### 11.1 与 HL1 渲染能力的对应

| GMod 库 | HL1 / Xash3D 可用能力 | 说明 |
|---------|----------------------|------|
| `surface.SetDrawColor / DrawRect / DrawOutlinedRect / DrawLine` | `gEngfuncs.pfnFillRGBABlend`、TriAPI 画线 | 矩形、线段 |
| `surface.SetMaterial / DrawTexturedRect` | `SPR_Set / SPR_DrawAdditive / SPR_DrawHoles`（精灵）；TriAPI 带纹理四边形 | HL1 无 VMT，**`Material("xxx")` 对应 spr 精灵或 tga/bmp 纹理** |
| `surface.CreateFont / SetFont / DrawText / GetTextSize` | 引擎控制台字体 `pfnDrawString`（⚠️ 字体固定）；自带字体渲染需求见下 | **v1 用引擎自带字体**；v2 内置 `stb_truetype` 生成字体图集再用 TriAPI 渲染 |
| `draw.SimpleText / RoundedBox / DrawText` | 用 `surface` 封装（纯 Lua） | |
| `render.DrawLine / DrawBeam / DrawSprite` | TriAPI（`Begin(TRI_LINES/TRI_QUADS)`、`Vertex3f`） | 3D 辅助线/光束 |
| `cam.Start3D / Start2D` | 无直接对应；v1 只支持在 `PostDrawOpaqueRenderables` 钩子里直接画 3D | |
| `ClientsideModel` | `gEngfuncs.CL_CreateVisibleEntity` / TempEnt | |
| `input.IsKeyDown / LookupBinding` | `gEngfuncs.Key_*`、`pfnGetKeyState` ⚠️ | |
| `gui.EnableScreenClicker` | `IN_ActivateMouse / IN_DeactivateMouse` + 自管光标 | |
| `ScrW / ScrH` | `gEngfuncs.GetScreenInfo` | |
| `render.RenderView` / `RT` / 着色器 | ❌ 无 | **不做**；这是 HL1 与 Source 差距最大的部分 |

### 11.2 UI（vgui / Derma）

HL1 的 VGUI1 已基本被弃用，**不要**去对接。计划：

1. **v1**：不提供面板系统，用 `HUDPaint` + `surface` 手绘，覆盖 90% 的 HUD 需求。
2. **v2**：纯 Lua 实现轻量 `vgui`：`DFrame`、`DPanel`、`DLabel`、`DButton`、`DTextEntry`、`DListView`，布局只做绝对坐标 + 简单 Dock；输入由 `HUD_Key_Event` 与鼠标位置驱动。

### 11.3 屏幕/HUD 示例

```lua
-- lua/autorun/client/xgl_hud.lua
hook.Add("HUDShouldDraw", "xgl_hide_default", function(name)
    if name == "CHudHealth" then return false end   -- 隐藏原版血量
end)

hook.Add("HUDPaint", "xgl_hud", function()
    local ply = LocalPlayer()
    if not IsValid(ply) then return end
    draw.SimpleText("HP: " .. ply:Health(), "Default", 20, ScrH() - 40, Color(255, 200, 0))
end)
```

---

## 12. GLua 语法兼容层

GLua（Garry's Mod 的 Lua）在标准 Lua 5.1 之上加了 C 风格语法糖，**大量 GMod 脚本依赖它**：

| GLua 写法 | 标准 Lua |
|-----------|----------|
| `a != b` | `a ~= b` |
| `a && b` / `a \|\| b` / `!a` | `a and b` / `a or b` / `not a` |
| `// 注释` | `-- 注释` |
| `/* 块注释 */` | `--[[ 块注释 ]]` |
| `continue` | 无（LuaJIT 有 `goto`，可转写） |

**实现方式：加载前的词法级预处理器**（`lua_syntax.cpp`），在把源码交给 `luaL_loadbuffer` 之前做一次 token 级改写：

- **必须保持行号不变**（只替换同一行内的 token，不增删换行），否则报错行号对不上；
- 正确处理字符串、长字符串 `[[ ]]`、注释内部（这些位置**不**改写）；
- `continue` 转写：在最近的 `for/while/repeat` 循环体末尾插入 `::__continue::`，把 `continue` 改成 `goto __continue`（⚠️ 注意 `repeat ... until cond` 里 `until` 能看到局部变量的特殊情形，以及嵌套循环要用不同的标签名，如 `__continue_<depth>`）；
- 默认**对所有脚本开启**（与 GMod 一致）；提供文件头标记 `--!nolua_ext` 关闭，以及 cvar `xgl_syntax_ext 0` 全局关闭；
- 预处理结果可缓存（按文件 CRC），避免重复计算。

---

## 13. 沙箱与安全

**威胁模型**：

1. **客户端被恶意服务器攻击**：服务端下发的 Lua 是**不可信代码**，不得读写任意文件、不得执行系统命令、不得加载原生库；
2. **服务端被第三方脚本包攻击**：社区下载的 autorun 可能是恶意的；
3. **玩家作弊**：客户端可能通过自己的脚本改 `CreateMove` 等（这是 GMod 同样面对的问题，不在范围内）；
4. **死循环 / 内存耗尽**导致服务器卡死。

### 13.1 环境表（沙箱）

脚本代码运行在**受限环境表**（`setfenv`）中，只放行白名单：

| 保留 | 剔除/改造 |
|------|-----------|
| `string table math bit coroutine pairs ipairs next select type tostring tonumber pcall xpcall error assert unpack setmetatable getmetatable rawget rawset rawequal rawlen` | **`ffi`（移除）** |
| XGLua 自己的库（`hook timer net ents ...`） | `io`、`os`（只保留 `os.time / os.date / os.clock`） |
| `file`（限定在 `data/` 下，见下） | `debug`（仅开发模式开放）、`package.loadlib`、`package.cpath` |
| `jit` 的只读子集（`jit.status`、`jit.version`） | `dofile`、`loadfile`、`load`（字节码）、`loadstring`/`load`（按需带权限）、`collectgarbage`（受限：仅 `count` ） |

> **为什么 FFI 必须在沙箱里移除**：LuaJIT 的 FFI 可以直接调用任意 C 函数、`mmap`/`mprotect`、加载任意动态库，相当于"完整的本机代码执行"。已有公开的 PoC 演示了在嵌入 LuaJIT 的应用里，只要脚本能访问 `ffi`，就能拿到进程权限。因此：**核心层与绑定层在沙箱构建之前使用 FFI，构建完成后，用户脚本所处的环境表里不包含 `ffi`**；也不要让任何白名单函数把 `ffi` 间接返回给脚本（如通过 `debug.getregistry`——所以 `debug` 也要拿掉）。

### 13.2 其他关键防护

| 项 | 做法 |
|----|------|
| **仅文本源码** | `luaL_loadbufferx(..., mode="t")`，**拒绝加载预编译字节码**（LuaJIT 字节码校验弱，恶意字节码可致内存破坏） |
| **文件系统** | `file.*` 经 `lua_vfs`：根目录固定为 `<gamedir>/data/`，规范化路径、禁止 `..`、禁止绝对路径、禁止符号链接跳出；限制扩展名白名单（`.txt .json .dat .lua? .png`），单文件大小与总配额限制 |
| **内存** | 自定义分配器，每 VM 内存上限；超限时抛出 Lua 错误（`not enough memory`）而非崩溃 |
| **CPU 看门狗** | 对**不可信代码**（客户端下发脚本、`RunString`）使用 `lua_sethook(L, hook, LUA_MASKCOUNT, N)`：在**解释器模式**下能中断死循环。注意：LuaJIT 开启 hook 会抑制 JIT 编译，所以只对不可信入口启用；**可信的核心层与已审阅的服务端脚本不启用**，以保留 JIT 性能。服务端另提供 `xgl_watchdog_ms`（可选，开发模式默认 2000ms）|
| **网络** | `net` 速率限制（见 10.2）；`http` 库默认**不加载**，管理员显式开启并配置域名白名单 |
| **下发脚本权限** | 客户端对服务端脚本的 `file.Write` 默认禁用；`RunConsoleCommand` 采用**黑名单**（`quit`、`exit`、`exec`、`alias`、`bind`、`connect`、`disconnect`、`rcon_*`、`cl_` 敏感项等）与速率限制 |
| **cvar 保护** | 脚本创建的 cvar 标记 `FCVAR_LUA`；脚本不能修改引擎核心 cvar（`fps_max`、`gl_*` 等白名单之外） |

### 13.3 信任模型小结

```
 includes/、autorun/（随 mod 发行）  ──  可信：完整 API，JIT 全开，不装看门狗
 服务端 gamemode/entities/weapons     ──  半可信：服务器管理员自己负责，沙箱仍然生效
 服务端下发给客户端的 Lua              ──  不可信：沙箱 + 看门狗 + 速率限制 + 白名单
 RunString / CompileString 传入代码    ──  不可信：同上
```

---

## 14. 调试与开发体验

### 14.1 控制台命令

| 命令 | Realm | 说明 |
|------|:----:|------|
| `lua_run <code>` | 服务端 | 在服务端执行一行 Lua（仅 rcon/单机） |
| `lua_run_cl <code>` | 客户端 | 在客户端执行一行 Lua |
| `lua_openscript <path>` | 服务端 | 执行指定 Lua 文件 |
| `lua_openscript_cl <path>` | 客户端 | 同上 |
| `xgl_reload` | 两端 | 重载全部脚本（保留实体，仅替换函数表） |
| `xgl_refresh <path>` | 两端 | 仅重载指定文件 |
| `xgl_dump_hooks` | 两端 | 打印 hook 注册表 |
| `xgl_mem` | 两端 | 打印 VM 内存、GC 状态 |
| `xgl_jit_status` / `xgl_jit_dump` | 两端 | JIT 诊断（开发模式） |
| `developer 1` | — | 同时打开 Lua 详细日志 |

### 14.2 错误报告

- 所有从 C++ 进入 Lua 的调用都经 `lua_pcall` + 自定义 `msgh`，**打印完整堆栈**（文件名:行号、函数名、局部变量名可选）；
- 格式与 GMod 类似：

```
[ERROR] lua/entities/xgl_medkit/init.lua:17: attempt to index a nil value
  1. Touch - lua/entities/xgl_medkit/init.lua:17
  2. unknown - [C]:-1
```

- 同一个 hook 在短时间内连续报错 N 次（默认 10），**自动禁用该 hook**并打印提示，避免刷屏与卡顿；`xgl_hook_errormax` 可调。

### 14.3 热重载

1. **文件监视**：`developer ≥ 1` 时，每 0.5 秒（`xgl_autorefresh_interval`）检查 `lua/`、`gamemodes/` 下文件修改时间；
2. **安全重载**：
   - `hook.Add(event, id, fn)` 同 id 覆盖（天然幂等）；
   - 实体/武器：重新加载其 `ENT`/`SWEP` 表，并把**已存活实例的元表指针**换成新表（保留实例字段）；
   - 触发 `OnReloaded` hook 让脚本自己重建 UI 等；
3. **文件缓存失效**：重载时清空 `include` 缓存与语法预处理缓存。

### 14.4 工具（建议）

- **VS Code 扩展**：提供 `xgl.d.lua`（EmmyLua / LuaLS 注解文件）实现补全；
- **FGD 生成器**：从 `lua/entities/*` 导出 Hammer FGD；
- **本地测试服务器脚本**（`xash3d -dedicated +map crossfire +xgl_gamemode ...`）。

---

## 15. 示例脚本

### 15.1 自动执行：给每个出生的玩家欢迎信息 + 自定义命令

```lua
-- lua/autorun/xgl_welcome.lua  （双端自动执行）

if SERVER then
    AddCSLuaFile()   -- 把本文件下发给客户端（如果客户端需要）

    util.AddNetworkString("xgl_welcome")

    hook.Add("PlayerInitialSpawn", "xgl_welcome", function(ply)
        timer.Simple(2, function()
            if not IsValid(ply) then return end
            net.Start("xgl_welcome")
                net.WriteString("欢迎 " .. ply:Nick() .. "！")
            net.Send(ply)
        end)
    end)

    concommand.Add("xgl_heal", function(ply)
        if IsValid(ply) and ply:Alive() then ply:SetHealth(100) end
    end)
else
    net.Receive("xgl_welcome", function()
        local msg = net.ReadString()
        hook.Add("HUDPaint", "xgl_welcome_draw", function()
            draw.SimpleText(msg, "Default", ScrW() / 2, 80, Color(255, 255, 255), TEXT_ALIGN_CENTER)
        end)
        timer.Simple(5, function() hook.Remove("HUDPaint", "xgl_welcome_draw") end)
    end)
end
```

### 15.2 简单的僵尸生存玩法骨架

```lua
-- gamemodes/zombie_survival/gamemode/init.lua
AddCSLuaFile("cl_init.lua")
AddCSLuaFile("shared.lua")
include("shared.lua")

local wave = 0

function GM:InitPostEntity()
    timer.Create("zs_wave", 30, 0, function()
        wave = wave + 1
        for i = 1, wave * 3 do
            local z = ents.Create("monster_zombie")        -- 原版 HL1 实体也可以用 ents.Create 创建
            z:SetPos(self:RandomSpawnPos())
            z:Spawn()
        end
        PrintMessage(HUD_PRINTTALK, "第 " .. wave .. " 波僵尸来袭！")
    end)
end

function GM:PlayerDeath(victim, inflictor, attacker)
    if IsValid(attacker) and attacker:IsPlayer() and attacker ~= victim then
        attacker:AddFrags(1)
    end
end

function GM:PlayerLoadout(ply)
    ply:Give("weapon_9mmhandgun")
    ply:GiveAmmo(60, "9mm")
    return true
end
```

> `ents.Create` 对**原版 C++ 实体**走 `CREATE_NAMED_ENTITY`（按导出符号），对 **Lua 实体**走 9.1 的 placement-new 路径；对 Lua 调用者透明。

---

## 16. 里程碑

| 里程碑 | 内容 | 验收标准 |
|--------|------|----------|
| **M0 准备**（1 周） | 选定 `hlsdk-portable` 基线；submodule 引入 LuaJIT；CI 打通 Windows / Linux x64 / Android arm64 静态链接 | 空 DLL 能带着 LuaJIT 被 Xash3D 加载，`print("hello")` 在控制台输出 |
| **M1 运行时 + 共享库**（2~3 周） | `lua_runtime`、VFS、沙箱、错误处理；`enum.lua`、`hook`、`timer`、`concommand`、`ConVar`、`Vector/Angle/Color`、`string/table/math` 扩展；`include/AddCSLuaFile` 基础 | 在服务端 `lua_run` 可调用上述库；hook 能响应 `Think` |
| **M2 实体/玩家 API**（3~4 周） | Entity/Player 句柄与方法子集；`ents`、`player`、`util.TraceLine`、`EntityTakeDamage` 等服务端 hook；`CLuaEntity` 与 `scripted_ents` | 写出上面 `xgl_medkit` 并正常工作；`ents.Create("monster_zombie")` 能生成原版怪物 |
| **M3 网络 + 客户端 VM**（3~4 周） | `net` 传输层（分片、速率限制）；Lua 文件下发（流式通道）；客户端 VM 生命周期；`HUDPaint` + `surface/draw` 基础 | 15.1 示例端到端跑通；`xgl_cl_allowdownload 0` 时拒绝下发 |
| **M4 游戏模式 + base** | `gamemode` 加载链；`base` 游戏模式复刻原版 DM；`NetworkVar`（快速槽 + 溢出）| 装上 XGLua、不写脚本时与原版 DM 行为一致；`zombie_survival` 骨架可玩 |
| **M5 武器** | `CLuaWeapon`、SWEP 注册、**P1 服务端权威**方案 | 能做出一把自定义武器（含自定义弹药、视图模型动画） |
| **M6 开发体验** | 热重载、GLua 语法层、FGD 生成器、LuaLS 注解、文档站 | 保存即生效；GMod 风格的 `!=`/`continue` 脚本直接可跑 |
| **M7 进阶** | SWEP **P2 预测**；轻量 vgui；`NPC` API；字体渲染 | 按需评估 |

---

## 17. 风险与待验证清单

| 风险/问题 | 影响 | 应对 |
|-----------|------|------|
| 动态类名实体无法通过导出符号创建（9.1） | 地图放置 Lua 实体受限 | 通用 `lua_entity` + FGD 生成 + `.ent` 覆盖；脚本创建走 placement new。**⚠️ 动手前阅读 xash3d-fwgs 的实体创建路径验证** |
| SWEP 客户端预测复杂（9.5） | 手感差、同步 bug | 先 P1 服务端权威；P2 严格限制可预测 API |
| 用户消息大小/数量限制（10.2） | 网络库吞吐低、可能撑爆可靠通道 | 单一 `XGLNet` 消息 + 分片 + 令牌桶；压测 |
| `entity_state_t` 用户字段被原版占用（9.4） | 快速槽可用数不足 | 盘点 `iuser/fuser/vuser` 的实际占用；不足就让溢出通道兜底 |
| iOS 禁 JIT、冷门架构无 LuaJIT（5.3） | 性能与可用性 | 解释器模式；降级 PUC Lua 5.1；绑定层双实现 |
| LuaJIT 无稳定版本号、滚动更新（5.1） | 升级引入回归 | 锁定 commit；CI 回归；保留 OpenResty 分支作为备选 |
| FFI 暴露即等于代码执行（13.1） | 严重安全问题 | 沙箱移除 `ffi`、`debug`；仅文本源码；下发脚本白名单 |
| 看门狗与 JIT 互斥（13.2） | 性能 vs 安全权衡 | 仅对不可信入口启用 count hook |
| Xash3D 客户端 DLL 调用时机（4.3） | 客户端 VM 创建时机不对 | 阅读 `HUD_VidInit`、`HUD_Init`、连接流程；必要时用首帧触发 |
| 字体渲染受限（11.1） | HUD 表现力有限 | v1 用引擎字体；v2 自带字体图集 |
| 存档/读档（9.1） | Lua 实体状态丢失 | v1 声明不支持；v2 用 `SAVERESTOREDATA` 序列化 |
| 无真实 SteamID（7.5） | 持久身份/权限系统 | 提供 `UniqueID` 机制；权限系统用 `xgl_admins.txt` 按 AuthID + 密码/token |
| 武器 ID 位宽受 `delta.lst` 限制（9.5 / 19.5） | Lua 武器数量上限 | 预留 ID 池 + 自带 `delta.lst`（位宽扩到 6 位）；超出数量的武器降级为服务端权威（`cl_lw` 关） |
| 原点/碰撞约定与 GMod 不同（6.4） | 移植脚本行为错误 | `GetFeetPos`、`xgl_gmod_origin` 兼容开关、文档与示例 |
| 协议 48/49 与用户消息大小差异（10.2 / 19.3） | 分片大小选错导致断线 | 默认保守 160 字节；协议/引擎探测后再放大；压测 |
| 64 位 mod 库需单独编译（18.1） | 发行物数量增多 | CI 矩阵自动产出；LuaJIT 按目标各编一次 |
| 许可证 | 合规 | LuaJIT：MIT；`hlsdk-portable`：Valve SDK License（沿用其条款）；Xash3D FWGS：GPLv3（只做动态加载关系，注意各平台分发条款） |

### 动手前建议先做的 5 个小实验（半天~1 天）

1. 在 `hlsdk-portable` 的 `GameDLLInit` 里创建 LuaJIT VM，执行 `print("hello")`，确认静态链接在 Windows / Linux / Android 都可行；
2. 用 `CREATE_ENTITY()` + placement new 创建一个 `CLuaEntity` 并 `Spawn`，验证不依赖导出符号；
3. 注册一条 `XGLNet` 用户消息，实测**单条最大载荷**与连续发送的可靠通道上限；
4. 读 `cl_dll` 与 Xash3D 的 `HUD_VidInit` / 连接流程，确定客户端 VM 的创建点；
5. 做一个最小 `HUDPaint`：在屏幕上画一个矩形和一行引擎字体文字。

---

## 18. 多平台与 Android

> 具体的目录、构建命令、需要改动的 SDK 文件见 `docs/INTEGRATION.md`；这里只列设计层面的决定。

### 18.1 支持矩阵

| 平台 | 产物 | Lua 后端 | 备注 |
|------|------|----------|------|
| Windows x86 / x64 | `hl.dll` `client.dll` | LuaJIT（JIT） | |
| Linux x86 / x64 / ARM / ARM64 | `.so` | LuaJIT（JIT） | 32 位需 `-m32` 与 32 位 host 工具 |
| macOS x64 / ARM64 | `.dylib` | LuaJIT（JIT） | |
| **Android** armv7 / arm64 / x86 / x86_64 | 打进 mod APK 的 `libserver` / `libclient` | LuaJIT（JIT） | 引擎由单独的 Xash3D FWGS APK 提供 |
| iOS / iPadOS | dylib | LuaJIT 解释器（`LUAJIT_DISABLE_JIT`） | 系统禁止 JIT |
| Switch / Vita / 3DS / Wii / Xbox / e2k / RISC-V | 视移植而定 | PUC Lua 5.1 后备（必要时再评估 LuaJIT 解释器） | 绑定层必须有无 FFI 实现 |

> **64 位提醒**：Xash3D FWGS 虽支持 64 位引擎，但官方文档明确说明 **mod 库必须针对对应位数重新编译**。所以每个发行版本都要为每个架构各出一份库，LuaJIT 也随之各编一次。

### 18.2 Android 专项设计

1. **打包形态**：沿用 hlsdk-portable 的 `android/` 工程；mod APK 通过 Intent 启动引擎 APK，并传入 `gamedir`。mod 的 `libserver` / `libclient` 由引擎 APK 加载。
2. **存储**：游戏数据放在用户存储中的 xash 目录（历史上是 `/sdcard/xash/<gamedir>`；新版 Android 的分区存储策略 ⚠️ 以引擎 APK 当前行为为准）。因此 **Lua 核心库内嵌进 `.so`**（用 `tools/pack_lua.py` 生成 C 数组），用户目录里的 `lua/` 只作扩展与覆盖，避免用户拷漏文件、也避免读取路径问题。
3. **文件系统**：所有 Lua 文件读取走**引擎文件接口**（支持 `.pk3` / `.pk3dir`，并带大小写不敏感模拟——Linux/Android 文件系统大小写敏感，而 GMod 脚本常常大小写混用）；`file.*` 写入仅限 `<gamedir>/data/`。
4. **触屏**：提供 `mobile` 库：`mobile.IsTouch()`、`mobile.Vibrate(ms)`（引擎有 Mobility API）；HUD 需考虑触控按钮布局，`HUDPaint` 示例要给出触屏适配写法。
5. **资源紧张**：Android 默认 VM 内存上限更低（服务端 96 MB / 客户端 64 MB，可配置）、GC 更积极、`Think/HUDPaint` 的单帧 Lua 预算告警（`xgl_budget_ms`）。
6. **JIT 在 Android 上可用**（含 arm64），所以不必像 iOS 那样降级；但仍要为每个 ABI 单独构建 LuaJIT。
7. **调试**：Android 上 `lua_run` 不方便，提供 `xgl_log_to_file 1` 把 Lua 日志写到 `data/xgl.log`，并提供 `adb logcat` 友好的前缀（`[XGLua]`）。

### 18.3 测试矩阵

- **纯 Lua 层**（`tests/lua/`）：在 CI 里用 `luajit` 直接跑，覆盖 `hook`/`timer`/`string`/`table`/语法预处理，不依赖引擎；
- **引擎冒烟测试**：桌面平台用 Xash3D dedicated server 加载 mod，跑一组 `lua_run` 断言；
- **Android**：CI 只保证 APK 构建通过；真机/模拟器测试放在发布前手工清单；
- **网络**：本地起 1 服务端 + 2 客户端（含一个高延迟/丢包条件，如 Linux `tc netem`）测试 `net` 分片与重发。

---

## 19. HL1 / GoldSrc 机制速查及其对设计的影响

> 资料来源：TWHL wiki / VERC 归档文章、VDC（Xash3D 与 GoldSrc 页面）、AlliedModders 的 HL1 引擎消息文档、hlsdk-portable 与 Featureful SDK 文档。详见第 20 章。

### 19.1 实体系统

| 机制 | 要点 | 对 XGLua 的影响 |
|------|------|-----------------|
| 类名 → C++ 类 | 地图用类名放置实体；`LINK_ENTITY_TO_CLASS(mapName, className)` 把类名链接到 C++ 类，并且**同一个 C++ 类可以被多个类名链接**（Valve 自己就这么做，如 `weapon_9mmhandgun` 与 `weapon_glock`） | Lua 的"简单变体"（换模型/血量/伤害）可直接复用原版 C++ 类，通过类名 + 键值覆盖，而不必每个变体都走 `CLuaEntity` |
| 变体实现的陷阱 | 变体类的 `Spawn` 里若强制设置了 `pev->classname` 或模型，会覆盖关卡设计师的键值 | `ents.Create` 对原版类设置键值后再 `Spawn`；文档提示该陷阱 |
| 存档 | 实体成员要在 `Save/Restore` 里通过 `TYPEDESCRIPTION` + `DEFINE_FIELD` 登记，否则读档/换图会损坏 | Lua 实体 v1 声明"不支持存档/换图保留"；v2 通过 `FIELD_STRING` 序列化 `luaclass` 与 Lua 状态 |
| 实体指针 | 原版代码大量用 `edict_t*` / `entvars_t*` / `CBaseEntity*` 互转，实体被删除后指针悬挂 | 采用第 6.2 的 `{index, serial}` 句柄，所有访问做有效性校验 |
| 命名与类名 | `classname`、`targetname`、`target` 是关卡逻辑的核心（`trigger_*`、`multi_manager`） | `ents.FindByName`、`ent:Fire(input)` 要与原版 `target/targetname` 机制互通；`ENT:KeyValue` 覆盖所有自定义键值 |

### 19.2 碰撞与 hull

- 世界碰撞使用 BSP 里**编译期预生成的 clip hull**：hull 0 = 点（物理面）、hull 1 = 站立玩家 32×32×72、hull 2 = 大型怪物 64×64×64、hull 3 = 蹲伏玩家 32×32×36（DMC 等 mod 的尺寸不同）。
- 实体之间的碰撞用 AABB；`MOVETYPE_STEP/WALK` 类实体与世界碰撞时，**使用最接近的预编译 hull**，而不是它自己设定的任意尺寸（TWHL 论坛上 `func_pushable` 的案例说明了这一点）。
- 关卡可以用 `info_hullshape` 自定义 hull 形状；VHLT 可通过 `-nohull#` 等关闭某些 hull。

**影响**：`SetCollisionBounds` 只影响实体间碰撞；`util.TraceHull` 只接受 `HULL_*` 常量；文档要明确"没有物理引擎，没有 `PhysObj`，`SetModelScale` 之类的碰撞缩放不可用"。

### 19.3 用户消息与协议

> **本地源码核验（未做端到端测试）：** 当前 `xash3d-fwgs-20/engine/common/protocol.h` 将 `PROTOCOL_VERSION` 定义为 49、`MAX_USER_MESSAGES` 定义为 197；`engine/common/common.h` 将服务端 `MAX_USERMSG_LENGTH` 定义为 2048。客户端边界处理可能不同，实际可注册数量、单条可用载荷和可靠通道容量仍需逐项核对与实测；下文的 191 和 2048 均不能视为已验证的运行时限制。

- GoldSrc 用户消息：`REG_USER_MSG(name, size)` 注册，`size = -1` 表示变长；单条最大 **192 字节**；`MESSAGE_BEGIN(MSG_ONE / MSG_ALL / MSG_BROADCAST …)` 发送，`MSG_*_UNRELIABLE` 变体不可靠；
- 协议：Xash3D FWGS 的主协议是 **49**；协议 48（Xash 旧版与 GoldSrc 兼容版）已不推荐；服务端原生只支持 49，混用 GoldSrc 48 的 mod 可借助 `gsmrf` 兼容模式把 48 的消息即时转换为 49；
- 引擎上限（VDC 页面所列）：用户消息种类 Xash3D 191；`MAX_EDICTS` 默认 600–8192，可通过 `gameinfo.txt` 调整；精灵/模型/声音预缓存上限均高于 GoldSrc。

**影响**：第 10 章的 `net` 传输层以 192 字节为保守基线，再按协议/引擎探测放大；只占用 1 个用户消息槽位，避免与原版及其他 mod 抢位。

### 19.4 状态同步（delta 压缩）

- 实体与客户端状态通过 `entity_state_t`、`entity_state_player_t`、`custom_entity_state_t`、`clientdata_t`、`weapon_data_t` 的**增量压缩**同步，字段与位宽由 **`delta.lst`** 描述，服务端启动时解析并同步给客户端；
- 因此：自定义网络字段 ≈ 复用已有结构体字段（`iuser/fuser/vuser` 等）+ 调整 `delta.lst`；
- 事件（`PLAYBACK_EVENT`）是 HL1 武器特效预测的核心通道，`FEV_RELIABLE` 可靠事件在带宽不足时会分片发送。

**影响**：9.4 的 `NetworkVar` 快速槽方案；`PLAYBACK_EVENT` 用于 Lua 武器的枪口火焰/弹壳等预测特效（客户端需要有对应的事件处理 C++ 或通用 Lua 事件）。

### 19.5 武器系统

- 服务端类层次：`CBasePlayerItem` → `CBasePlayerWeapon`；每把武器 `GetItemInfo` 填 `iSlot/iPosition/iMaxClip/iId/...`，并通过 `WeaponList` 用户消息告知客户端 HUD；
- 客户端预测：`cl_dll/hl/hl_weapons.cpp` 中保留一份几乎相同的武器逻辑（`HUD_WeaponsPostThink`），`cl_lw` 控制是否启用；
- 动画是**模型序列号**，由 QC 的顺序决定，而不是 `ACT_*` 名字；玩家持握模型有固定的几种（`crowbar / trip / onehanded / python / shotgun / gauss / mp5 / rpg / egon / squeak / hive / bow`）。

**影响**：见 9.5。`SWEP.PlayerModel` 要提供合法取值校验。

### 19.6 引擎内置限制速查（Xash3D vs GoldSrc）

| 项目 | GoldSrc（旧 / 新） | Xash3D（FWGS 视配置） |
|------|--------------------|------------------------|
| `MAX_EDICTS` 默认 | 900 / 1200 | 600–8192（服务端下发，客户端自适应） |
| `MAX_EDICTS` 绝对上限 | 2048 | 8192 |
| 用户消息种类 | 128 | 191 |
| 预缓存模型 | 512 | 1024（FWGS 4096） |
| 预缓存声音 | 512 | 2048 |
| 可见实体包大小 | 256 / 1024 | 2048 |

> 数值为 VDC 页面与社区资料所列的"典型值"，**会随版本与配置变化**，只作设计量级参考；`ents` 库与 `net` 库不要把任何一个上限硬编码，应在运行时读取引擎当前值。

---

## 20. 参考资料

- Xash3D FWGS 引擎与文档：https://github.com/FWGS/xash3d-fwgs
- hlsdk-portable（基线 SDK）：https://github.com/FWGS/hlsdk-portable
- Featureful SDK 与其 `delta.lst` 文档：https://github.com/FreeSlave/halflife-featureful ，https://freeslave.github.io/halflife-featureful/docs/configuration/delta-lst
- Half-Life Unified SDK：https://github.com/twhl-community/halflife-unified-sdk
- VDC：Xash3D 页面 https://developer.valvesoftware.com/wiki/Xash3D ；GoldSrc Programming 分类 https://developer.valvesoftware.com/wiki/Category:GoldSrc_Programming
- TWHL wiki：Entity Programming - Overview（https://twhl.info/wiki/page/Entity_Programming_-_Overview/6084）；Weapons Programming - High-Level Overview（https://twhl.info/wiki/page/Weapons_Programming_-_High-Level_Overview）；Weapons Programming - Custom Ammo Types（https://twhl.info/wiki/page/Weapons_Programming_-_Custom_Ammo_Types）；Wavelength: How to create a server-client message（https://twhl.info/wiki/page/Wavelength%3A_How_to_create_a_server-client_message）；VERC: Adding User Variables to Half-Life（https://twhl.info/wiki/page/VERC:_Adding_User_Variables_to_Half-Life）；HULL #（https://twhl.info/wiki/page/HULL_/8169）
- TWHL 论坛：Half-Life Updated（https://twhl.info/thread/view/20055）——关于 `CLIENT_WEAPONS` / `cl_lw` 的讨论
- AlliedModders：Half-Life 1 Engine Messages（https://wiki.alliedmods.net/Half-Life_1_Engine_Messages）——delta 压缩与引擎消息格式
- GoldSrc `gamerules.h` 源码（P4HL 镜像等）——`CGameRules` 虚函数列表

> 说明：Source 引擎（HL2/GMod）一侧的 VDC 页面（如 Prediction、Networking Events & Messages）讲的是 **Source** 的机制，**不能**直接套用到 GoldSrc；本文只用它们做 GMod 行为对照。

---

*文档结束。后续可拆分为：`API_REFERENCE.md`（按库列出全部函数签名）、`NETWORK_PROTOCOL.md`（XGLNet 线格式）、`SECURITY.md`（沙箱细则）。*
