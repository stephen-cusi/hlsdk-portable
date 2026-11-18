# Server Lua console API

Register a callback with `concommand.Add(name, callback)`. Invoke it from the
server console as `lua_cmd <name> [args...]`. The callback receives
`(nil, name, args, argString)`: `args` is a one-based array of engine command
tokens and `argString` joins those tokens with spaces. Original quoting is not
retained. Ordinary remote client commands do not enter this bridge.

Names have 1 to 63 lowercase ASCII letters, digits, or underscores. Adding a
name again replaces its callback. `concommand.Remove(name)` removes it. Each
map VM supports 128 registrations and each invocation accepts 64 arguments.
Registrations vanish when the map VM closes. Only `lua_cmd` is an engine
command; Lua names do not register additional native commands.

`GetConVar(name)` returns a read-only snapshot with `GetName()`, `GetString()`,
`GetFloat()`, `GetInt()` (truncate toward zero), and `GetBool()` (nonzero).
Call `GetConVar` again for a fresh snapshot. Absent, disallowed, or protected
variables return `nil`. The allowlist is `deathmatch`, `developer`,
`mp_friendlyfire`, `skill`, `sv_gravity`, `sv_maxspeed`, and `xgl_gamemode`.
This API cannot create or modify engine variables or execute engine commands.
