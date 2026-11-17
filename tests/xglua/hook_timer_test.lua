local script = (arg[0] or ""):gsub("\\", "/")
local project = script:match("^(.-)tests/xglua/[^/]+$")
local root = (arg[1] or (project and (project .. "lua/includes/modules")))
assert(root, "Run from the repository root or supply the module directory")
root = root:gsub("\\", "/")
local failures = 0
local function test(name, f)
 local ok, err = pcall(f)
 if ok then print('PASS ' .. name) else failures = failures + 1; print('FAIL ' .. name .. ': ' .. tostring(err)) end
end
local now = 0
CurTime = function() return now end
local function reload()
 XGLUA_MODULE_HOOK = nil; XGLUA_MODULE_TIMER = nil; hook = nil; timer = nil; GAMEMODE = nil; now = 0
 dofile(root .. '/hook.lua'); dofile(root .. '/timer.lua')
end
local function pack(...) return { n = select('#', ...), ... } end

test('numeric hook identifiers', function()
 reload(); local count = 0
 hook.Add('event', 1, function() count = count + 1 end)
 hook.Add('event', 2, function() count = count + 1 end)
 hook.Run('event'); assert(count == 2, 'expected both numeric callbacks, got ' .. count)
end)

test('reentrant hook preserves outer args', function()
 reload(); local checked = 0
 hook.Add('inner', 'one', function() end)
 for i = 1, 3 do hook.Add('outer', 'id' .. i, function(a,b,c)
  assert(a == 'outer' and b == nil and c == 'tail', 'outer arguments changed')
  checked = checked + 1; hook.Run('inner', 'inner')
 end) end
 hook.Run('outer', 'outer', nil, 'tail'); assert(checked == 3, 'missing callbacks')
end)

test('hook preserves trailing nil results', function()
 reload(); hook.Add('event', 'id', function() return 'value', nil, 'tail', nil end)
 local r = pack(hook.Run('event')); assert(r.n == 4 and r[1] == 'value' and r[2] == nil and r[3] == 'tail' and r[4] == nil, 'return tuple lost trailing nil')
end)

test('hook error counts distinguish numeric and string ids', function()
 reload()
 local originalPrint = print; print = function() end
 local calls = { number = 0, string = 0 }
 hook.Add('event', 1, function() calls.number = calls.number + 1; error('deliberate') end)
 hook.Add('event', '1', function() calls.string = calls.string + 1; error('deliberate') end)
 for i = 1, 5 do hook.Run('event') end
 print = originalPrint
 assert(calls.number == 5 and calls.string == 5 and hook.GetTable().event[1] and hook.GetTable().event['1'], 'distinct identifiers share error count')
end)

test('hook error does not remove replacement', function()
 reload(); local originalPrint = print; print = function() end
 local replacement = function() return 'replacement' end
 local errors = 0
 hook.Add('event', 'id', function()
  errors = errors + 1
  if errors == 10 then hook.Add('event', 'id', replacement) end
  error('deliberate')
 end)
 for i=1,9 do hook.Run('event') end
 hook.Run('event'); print = originalPrint
 assert(hook.GetTable().event.id == replacement)
end)

test('hook fallback and false returns', function()
 reload(); hook.Add('event', 'id', function() return false, nil end)
 local r = pack(hook.Call('event', { event = function() error('must not run') end }))
 assert(r.n == 2 and r[1] == false and r[2] == nil)
 hook.Remove('event', 'id')
 local gm = { event = function(self, ...) assert(self.marker); return ... end, marker = true }
 r = pack(hook.Call('event', gm, 'a', nil)); assert(r.n == 2 and r[1] == 'a' and r[2] == nil)
end)

test('timer preserves nil arguments', function()
 reload(); local args
 timer.Create('args', 1, 1, function(...) args = pack(...) end, 'a', nil, 'c', nil)
 now=1; timer.Tick(); assert(args and args.n == 4 and args[1] == 'a' and args[2] == nil and args[3] == 'c' and args[4] == nil, 'timer lost nil arguments')
end)

test('pause freezes timer remaining time', function()
 reload(); local called = 0; timer.Create('paused', 10, 1, function() called = called + 1 end)
 now=3; assert(timer.Pause('paused')); now=30; assert(timer.UnPause('paused')); timer.Tick()
 assert(called == 0 and timer.TimeLeft('paused') == 7, 'timer resumed without remaining delay')
 now=37; timer.Tick(); assert(called == 1)
end)

test('paused adjust preserves remaining interval', function()
 reload(); timer.Create('paused', 10, 1, function() end)
 now=4; timer.Pause('paused'); now=100; timer.Adjust('paused', 20); timer.UnPause('paused')
 assert(timer.TimeLeft('paused') == 12, 'pause-adjust did not preserve fraction')
end)

test('timer toggle and stop preserve remaining time', function()
 reload(); timer.Create('toggle', 10, 1, function() end)
 now=2; assert(not timer.Toggle('toggle')); now=20; assert(timer.Toggle('toggle'))
 assert(timer.TimeLeft('toggle') == 8); now=22; assert(timer.Stop('toggle'))
 now=50; assert(timer.UnPause('toggle')); assert(timer.TimeLeft('toggle') == 6)
end)

test('timer adjust retains explicit nil tuple', function()
 reload(); local args; timer.Create('args', 1, 1, function(...) args=pack(...) end)
 timer.Adjust('args', nil, nil, nil, nil, 'b', nil); now=1; timer.Tick()
 assert(args and args.n == 3 and args[1] == nil and args[2] == 'b' and args[3] == nil)
end)

test('nested tick preserves timer arguments', function()
 reload(); local args
 timer.Create('outer', 1, 1, function(...)
  timer.Create('inner', 0, 1, function() end, 'inner')
  timer.Tick(); args = pack(...)
 end, 'a', nil, 'c', nil)
 now=1; timer.Tick(); assert(args and args.n == 4 and args[1] == 'a' and args[3] == 'c', 'nested timer lost arguments')
end)
if failures > 0 then print(failures .. ' failures'); os.exit(1) end
print('hook and timer foundation tests passed')
