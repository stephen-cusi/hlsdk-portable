# XGLua 项目操作说明

- 项目：`D:\project\xglua-project\hlsdk-portable`；LuaJIT：`D:\project\xglua-project\xglua-luajit`；游戏：`D:\games\xash`。设计和 M0–M7 路线见 `doc/XGLua_Design.md` 第 16 节；设计文档是草案，当前状态以源码、Git 和实际验证为准。
- 原则：薄 C++、厚 Lua；服务端和客户端各有独立 VM。所有 C++ 调 Lua 必须使用 `XGLua_ProtectedCall` 保护边界；实体句柄须验证 serial。宿主预测测试保持无 `hud.h` / `vgui.lib` 依赖。`SWEP.Slot` 从 0 开始。
- 开发环境为 Windows ARM64。确认 CMake 缓存的架构和 `XGLUA_LUAJIT_DIR` 后，使用 `cmake --build build-migrated --config Release --target server client`。产物是 `build-migrated/dlls/Release/hl_arm64.dll` 和 `build-migrated/cl_dll/Release/client_arm64.dll`。LuaJIT Windows 产物为 `lua_shared.dll` / `.lib`。
- Lua 修改后用项目 LuaJIT 的 `loadfile` 作语法检查；按改动重新编译并运行相关 `tests/xglua` 宿主测试。运行已有 exe 不能证明当前源码通过。测试框架目前依赖 Windows，不应直接假定 Linux 可编译。
- 游戏内操作只由玩家执行；Agent 不控制游戏界面，不创建完整游戏副本。可提供 Lua 或 cfg 测试脚本。部署前检查 `xash3d` 进程，运行中不要覆盖 DLL；部署后逐项比对源和目标的 MD5。使用现有 `D:\games\xash`，不主动关闭玩家进程。
- UI 测试脚本应可重复运行：创建前移除已有面板，清理时关闭 ScreenClicker。客户端通过 `lua_run_cl include("tests/<文件>.lua")` 运行脚本；`lua_openscript` 是服务端命令。
- 2026-11-20：Windows ARM64 双端构建通过；21 组宿主测试从当时源码重建，181 个用例、1104 项检查通过。玩家确认 NPC 功能正常，UI 光标可见、移动鼠标不再驱动视角，日志确认按钮点击。面板关闭后的视角恢复、焦点/菜单共存和断线清理尚未明确实机验证。
- M8 尚未完成：客户端跨平台扫描、CI 双端构建和回归接入仍待处理；服务端扫描已通过 Linux ARM64 构建与符号链接测试，仍需 Android 和其他目标验证。不要将未经验证的中间改动当作完成。
