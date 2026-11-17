# XGLua 服务端 include 契约

本阶段 `include(path)` 的 `path` 相对于 `<gamedir>/lua/`，与已有脚本兼容；它不会按调用脚本目录自动解析。文件不存在、读取失败、语法或运行时错误会作为 Lua 错误传播，可由 `pcall` 捕获。被包含 chunk 的所有返回值（包括中间和尾部 nil）均原样返回；空文件有效，UTF-8 BOM 可选。

为了确定地从一个目录加载，可使用 XGLua 扩展 `include(path, baseDirectory)`，例如：

```lua
local config = include("shared.lua", "weapons/example")
local common = include("../common.lua", "weapons/example")
```

`baseDirectory` 本身从 `lua/` 根开始解释；指定后只加载归一化得到的路径，不会在根目录寻找同名文件。`..` 可以回到 `lua/` 内的上级，不能越过 `lua/`。绝对路径、盘符、NUL、控制字符、过长路径、以及以空格或点结尾的非 `.`/`..` 路径分量会被拒绝；斜杠和反斜杠都可作为分隔符。

路线图中的“自动相对调用脚本目录，再回退根目录”尚未实现。LuaJIT 尾调用会删除调用者帧，直接读取调用栈可能误认其他调用者，静默加载错误的同名脚本；显式目录能在普通调用、尾调用和延迟回调中保持一致。这是 XGLua 的扩展，不宣称与 GMod 的 `include` 接口完全一致。

所有文件仍使用同一个 Lua 全局环境，不修改 `getfenv`、`setfenv` 或 `_G` 的行为。路径检查只做词法约束，不是安全沙箱：当前服务器脚本仍完全受信任，尚未限制符号链接、`io`、`os`、`ffi` 等其他访问途径；读取目前使用系统文件接口，尚不是完整引擎 VFS。

`lua/includes/init.lua` 缺失、无法读取、编译失败或执行失败时，启动会记录错误、关闭部分初始化的 Lua VM，并且不会报告 VM ready。其他单独脚本加载失败会记录错误；由 `include` 调用的失败会向调用者传播。
