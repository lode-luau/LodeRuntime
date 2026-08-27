--!strict
-- Native module require-resolution regression test (canonical, runs through
-- the real runtime path via CTest). Exercises @self/utils, ./sibling_module,
-- and the native Signal/Task integration exposed by native_module.

local function check(condition: boolean, message: string)
    if not condition then
        error("native_require: " .. message, 2)
    end
end

local ok, NativeMod = pcall(require, "./native_module")
check(ok, "native_module did not load: " .. tostring(NativeMod))

-- Test 1: @self/utils should have been loaded during LODE_MODULE init.
local utils = NativeMod.utils
check(type(utils) == "table", "@self/utils was nil or wrong type (got " .. type(utils) .. ")")
check(utils.double(21) == 42, "utils.double(21) did not return 42")
check(utils.formatGreeting("World"):find("World", 1, true) ~= nil, "utils.formatGreeting did not include the name")

-- Test 2: ./sibling_module should have been loaded during LODE_MODULE init.
local sibling = NativeMod.sibling
check(type(sibling) == "table", "./sibling_module was nil or wrong type (got " .. type(sibling) .. ")")
check(sibling.name == "sibling_module", "sibling.name mismatch")
check(sibling.answer == 42, "sibling.answer mismatch")

-- Test 3: greet should delegate to @self/utils via C++ CallMethod.
local greeting = NativeMod.greet("Yan")
check(greeting:find("@self/utils") ~= nil, "greet did not route through utils (fallback active)")

-- Test 4: Native Signal integration.
local onTick = NativeMod.onTick
check(type(onTick) == "table" and onTick.Connect ~= nil, "onTick signal was nil or wrong type")

-- 4a: the proxy must be read-only (equivalent to table.freeze from Public()).
check(not pcall(function() onTick.Fire = function() end end), "onTick.Fire should be blocked from writes")
check(not pcall(function() rawset(onTick, "Fire", function() end) end), "rawset on onTick.Fire should be blocked")
check(onTick.Fire == nil, "onTick.Fire should not exist on the proxy")

-- 4b: Connect + Connected + connection read-only protection.
local connectFired = false
local conn = onTick:Connect(function()
    connectFired = true
end)
check(not pcall(function() conn.Connected = false end), "conn.Connected should be read-only")
check(not pcall(function() conn.Foo = 1 end), "writing a new field on conn should be blocked")
check(getmetatable(conn) == "connection", "conn metatable should be protected")
check(not pcall(function() setmetatable(conn, {}) end), "setmetatable(conn) should be blocked")

NativeMod.fireOnTick("connect check")
check(connectFired, "connect callback did not fire")
check(conn.Connected == true, "conn.Connected should stay true after a regular Fire")

-- 4c: Once fires only once.
local onceCount = 0
onTick:Once(function()
    onceCount += 1
end)
NativeMod.fireOnTick("once 1")
NativeMod.fireOnTick("once 2")
check(onceCount == 1, "Once should fire exactly once")

-- 4d: Disconnect stops the callback and flips Connected.
local disconnectFired = false
local dConn = onTick:Connect(function()
    disconnectFired = true
end)
dConn:Disconnect()
NativeMod.fireOnTick("after disconnect")
check(not disconnectFired, "Disconnect should stop the callback")

-- 4e: Wait resumes with the fire arguments.
local task = require("../../modules/task")
local waitedArgs: { any }? = nil
task.spawn(function()
    waitedArgs = { onTick:Wait(2) }
end)
NativeMod.fireOnTick("wait payload")
task.wait(0.05)
check(waitedArgs ~= nil and waitedArgs[1] == "wait payload" and #waitedArgs == 1, "Wait did not return the fire args")

-- 4f: Wait times out and returns nil.
local timedOut = false
task.spawn(function()
    timedOut = (onTick:Wait(0.05) == nil)
end)
task.wait(0.2)
check(timedOut, "Wait should time out with nil")

-- 4g: the native delayed task fires the signal after 0.5s.
local autoFired = false
onTick:Connect(function()
    autoFired = true
end)
for _ = 1, 40 do
    if autoFired then
        break
    end
    task.wait(0.05)
end
check(autoFired, "native module's delayed task did not fire the signal in time")

print("native_require: all checks passed")
