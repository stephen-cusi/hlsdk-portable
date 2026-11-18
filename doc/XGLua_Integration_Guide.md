# XGLua 集成与项目结构指南

> 适用基线：`FWGS/hlsdk-portable`（CMake 与 waf 双构建系统，自带 `android/` Gradle 工程）。
> 带 ⚠️ 的位置请对照你所用分支的实际源码确认。

## 1. 仓库关系

```
FWGS/xash3d-fwgs      引擎（不改，直接用官方发行/每日构建）
FWGS/hlsdk-portable   ← 你 fork 它，得到 "yourmod-hlsdk"（本项目的宿主仓库）
        └── 叠加本骨架：cmake/ common/lua/ dlls/lua/ cl_dll/lua/ gamedir/ tools/ ...
```

参考但**不要**作为基线的 SDK：
- **Featureful SDK**（基于 hlsdk-portable，带自定义 `delta.lst`、配置化实体）——适合借鉴 delta.lst 与实体设计；
- **Half-Life Unified SDK**（twhl-community）——功能强，但面向 Steam 版 GoldSrc/桌面平台，不覆盖 Xash 的移动端/小众平台。

## 2. 最终目录结构（宿主仓库）

```
yourmod-hlsdk/
├── CMakeLists.txt                  # 上游文件；末尾追加 include(cmake/xglua.cmake)
├── wscript                         # 上游 waf（可选；XGLua 优先支持 CMake）
├── cmake/
│   ├── xglua.cmake                 # xglua_attach(target SERVER|CLIENT)
│   └── xglua_luajit.cmake          # 后端选择与构建：luajit | lua51
├── 3rdparty/
│   ├── luajit/                     # submodule，锁 commit
│   └── lua51/                      # PUC Lua 5.1.5（后备）
├── common/lua/                     # 双端共享 C++（VM/VFS/沙箱/类型/网络编码）
├── dlls/                           # 上游服务端；新增 dlls/lua/
│   └── lua/
├── cl_dll/                         # 上游客户端；新增 cl_dll/lua/
│   └── lua/
├── pm_shared/ engine/ common/ ...  # 上游其余目录，不动
├── gamedir/                        # ★ 随 mod 发行的游戏目录内容（安装时拷到 <gamedir>/）
│   ├── liblist.gam / gameinfo.txt  # mod 描述（可在这里提高引擎资源上限，见第 6 节）
│   ├── delta.lst                   # ★ 自定义网络字段位宽（见设计文档 9.4/9.5）
│   ├── lua/{includes,autorun,entities,weapons}
│   ├── gamemodes/base/…
│   └── data/                       # file.Write 沙箱根（运行时创建）
├── android/                        # 上游 Gradle 工程（改 applicationId / gamedir / label）
├── tools/                          # gen_fgd.py / pack_lua.py / check_delta.py
├── tests/lua/                      # 纯 Lua 测试（CI 里用 luajit 直接跑）
├── docs/                           # 设计文档、本文件、API 参考
└── .github/workflows/build.yml     # CI 矩阵
```

## 3. 需要改动的 hlsdk-portable 位置（最小侵入）

| 文件 | 改动 | 说明 |
|------|------|------|
| `dlls/CMakeLists.txt` | `xglua_attach(${SERVER_LIBRARY} SERVER)` | 把 XGLua 源码并入服务端库 |
| `cl_dll/CMakeLists.txt` | `xglua_attach(${CLIENT_LIBRARY} CLIENT)` | 并入客户端库 |
| `dlls/game.cpp` ⚠️ `GameDLLInit` | `XGL_ServerInit()` | 创建 VM |
| `dlls/client.cpp` ⚠️ `ServerActivate` / `ServerDeactivate` / `StartFrame` | `XGL_ServerActivate()` / `XGL_ServerShutdown()` / `XGL_ServerFrame()` | 生命周期与 Think |
| `dlls/client.cpp` ⚠️ `ClientConnect/PutInServer/Disconnect/Command/Kill` | 转 `hook.Run` | 玩家事件 |
| `dlls/gamerules.cpp` ⚠️ `InstallGameRules` | 若 `xgl_gamemode` 有效则 `new CLuaGameRules` | 游戏模式 ↔ `CGameRules`（见设计文档 8.1.1） |
| `dlls/weapons.cpp` / `player.cpp` | 武器 ID 池、`CLuaWeapon` 注册 | 见设计文档 9.5 |
| `dlls/combat.cpp` ⚠️ `TakeDamage` 路径 | `EntityTakeDamage` hook | 可返回阻止 |
| `cl_dll/cdll_int.cpp` ⚠️ | `HUD_Init` / `HUD_VidInit` / `HUD_Frame` / `HUD_Shutdown` 挂接 | 客户端 VM 生命周期 |
| `cl_dll/hud_redraw.cpp` ⚠️ | `HUDPaint` / `HUDShouldDraw` | |
| `cl_dll/view.cpp` ⚠️ `V_CalcRefdef` | `CalcView` | |
| `cl_dll/input.cpp` ⚠️ `CL_CreateMove` | `CreateMove` | |
| `cl_dll/tri.cpp` ⚠️ | `PostDrawOpaque/Translucent` | TriAPI 3D 绘制 |
| `cl_dll/hl/hl_weapons.cpp` ⚠️ `HUD_WeaponsPostThink` | Lua 武器的客户端预测入口（P2 阶段） | |
| `gamedir/delta.lst` | 拷自 Featureful/Valve 并按需调整 | 武器 ID 位宽、NetworkVar 快速槽 |

## 4. 构建

### 4.1 桌面（CMake）
```
git clone --recursive <yourmod-hlsdk>
# Windows x86（Xash/GoldSrc 的 mod 库历史上多为 32 位）
cmake -A Win32 -B build -DGAMEDIR=mymod -DCMAKE_INSTALL_PREFIX="<Xash3D 安装目录>"
cmake --build build --config Release --target install
# Linux x86（32 位）
sudo apt install cmake build-essential gcc-multilib g++-multilib libsdl2-dev:i386
cmake -DCMAKE_BUILD_TYPE=Release -B build -S . && cmake --build build
```
注意：**Xash3D FWGS 支持 64 位引擎，但 mod 库必须重新编译成对应位数**，所以 CI 里要同时出 x86 与 x64（及 ARM）产物；LuaJIT 也跟着每个目标各编一次。

### 4.2 Android
1. 使用 hlsdk-portable 的 `android/` 工程（Android Studio 打开，或 `cd android && ./gradlew assembleRelease`）。
2. 修改：`settings.gradle` 的 `rootProject.name`、`app/build.gradle` 的 `namespace`/`applicationId`、`MainActivity.java` 里 `putExtra("gamedir", ...)`、`AndroidManifest.xml` 的 label 与 `su.xash.engine.gamedir`。
3. 架构分发：本 mod APK 只带 `libserver`/`libclient`（含静态链接的 LuaJIT），**引擎由单独的 Xash3D FWGS APK 提供**，mod APK 通过 Intent 启动引擎并指定 gamedir。
4. LuaJIT 为每个 ABI 各编一次：`armeabi-v7a`（需 32 位 host 工具链：`HOST_CC="gcc -m32"`）、`arm64-v8a`、`x86`、`x86_64`；arm64 默认 GC64。
5. 游戏数据在用户存储（历史上是 `/sdcard/xash/<gamedir>`，新版 Android 受分区存储影响 ⚠️ 以引擎 APK 当前行为为准）。**Lua 核心库建议内嵌进 `.so`（`tools/pack_lua.py` 生成 C 数组），用户目录的 `lua/` 仅用于扩展/覆盖**——避免用户拷贝漏文件。
6. 触屏 HUD：`input`/`mobile` 库（`mobile.IsTouch()`、`mobile.Vibrate(ms)`）；自定义按键用 `RunConsoleCommand("touch_addbutton", ...)` 等引擎触控命令（⚠️ 命令名以引擎文档为准）。

### 4.3 iOS / 其他平台
- iOS：禁 JIT，`XGLUA_DISABLE_JIT=ON`（LuaJIT 解释器），游戏库以 dylib 形式放入 `build/ios/libs/<gamedir>/{dlls,cl_dlls}`（见 xash3d-fwgs 的 iOS 构建说明）。
- Switch / Vita / 3DS / Wii / Xbox / e2k / RISC-V 等：默认回落 `lua51` 后端；是否可用 LuaJIT 逐平台评估（多数无可执行内存页）。

## 5. 运行时多平台注意事项（写代码时的硬规则）

1. **文件读取一律走引擎文件接口**（`LOAD_FILE_FOR_ME` / 客户端 `COM_LoadFile` 一类），不要 `fopen`：这样才支持 `.pk3` / `.pk3dir`、Linux/Android 的大小写不敏感模拟和 Android 路径。
2. **路径一律小写 + `/` 分隔**，`include("Foo/Bar.lua")` 在 VFS 层统一规范化。
3. **字节序/对齐**：`net` 编码用显式小端位流，不依赖结构体布局。
4. **内存上限**按平台设默认值（见 `xgl_config.h`），移动端更低；`collectgarbage("setpause"/"setstepmul")` 调成更积极。
5. **帧时间预算**：`Think`/`HUDPaint` 的 Lua 时间在移动端要有上限，超预算打印警告（`xgl_budget_ms`）。
6. **触屏输入**：`HUDShouldDraw`/`PlayerBindPress` 在触屏下没有键盘语义，脚本需通过 `mobile.IsTouch()` 分支。
7. **线程**：Lua VM 只在引擎主线程使用；任何异步（下载、压缩）只能由 C++ 工作线程完成后在主线程回调。
8. **SDL/输入**：不要在 mod 库里自己链接 SDL（hlsdk-portable 的客户端用引擎提供的输入接口）。

## 6. gameinfo / liblist 与资源上限

Xash3D 的 `MAX_EDICTS`、`MAX_MODELS`、`MAX_SOUNDS` 等上限可以通过 `gameinfo.txt` 调整（GoldSrc 原版只能 `liblist.gam`，且上限更低）。Lua 实体/特效多的玩法需要在 `gamedir/gameinfo.txt` 里调高，并写进发行说明；设计时按 **GoldSrc 保守值** 估算（编辑上限约 900~2048），Xash 下可放宽。

## 7. CI 矩阵（`.github/workflows/build.yml` 已给出雏形）

| Job | 产物 | 备注 |
|-----|------|------|
| lua-tests | — | `luajit tests/lua/*.lua`，不依赖引擎 |
| linux-x86 / linux-x64 / linux-arm64 | `.so` | 32 位用 `-m32`，ARM 用交叉或原生 runner |
| windows-x86 / windows-x64 | `.dll` | 对应 VS 架构的开发者环境（LuaJIT 用 `msvcbuild.bat static`） |
| macos-arm64 / macos-x64 | `.dylib` | |
| android | `.apk` | `./gradlew assembleRelease` |
| (可选) ios | `.ipa` | 解释器模式 |

发行包 = 各平台库 + `gamedir/` 内容（zip）+ Android APK。

## 8. 版本与依赖管理

- LuaJIT：submodule 锁 commit；升级走 PR + 全平台 CI + 烟雾测试（跑 `tests/`）。
- 内部版本号：`XGL_VERSION`（语义化），写入 `net` 握手，客户端与服务端不匹配时拒绝下发脚本。
- 许可证：LuaJIT MIT；hlsdk-portable 沿用 Valve SDK License；发行时附带第三方许可证清单。
