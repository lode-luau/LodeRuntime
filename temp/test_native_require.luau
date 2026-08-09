-- Diagnostic test for native module require resolution
-- Tests: @self/utils and ./sibling_module loaded from inside the native C++ module

local ok, NativeMod, MetaInfo = pcall(require, "./native_module")
if not ok then
    print("FAIL: native_module did not load:", NativeMod)
    return
end

print("native_module loaded OK")

-- Test 1: @self/utils should have been loaded during LODE_MODULE init
print("")
print("--- Test 1: @self/utils (exports.utils) ---")
local utils = NativeMod.utils
print("  type(NativeMod.utils) =", type(utils))
if type(utils) == "table" then
    print("  PASS: @self/utils loaded as table")
    print("  utils.describe() =", utils.describe())
    print("  utils.double(21) =", utils.double(21))
    print("  utils.formatGreeting('World') =", utils.formatGreeting("World"))
else
    print("  FAIL: @self/utils was nil or wrong type (got", type(utils), ")")
end

-- Test 2: ./sibling_module should have been loaded during LODE_MODULE init
print("")
print("--- Test 2: ./sibling_module (exports.sibling) ---")
local sibling = NativeMod.sibling
print("  type(NativeMod.sibling) =", type(sibling))
if type(sibling) == "table" then
    print("  PASS: ./sibling_module loaded as table")
    print("  sibling.name    =", sibling.name)
    print("  sibling.message =", sibling.message)
    print("  sibling.answer  =", sibling.answer)
else
    print("  FAIL: ./sibling_module was nil or wrong type (got", type(sibling), ")")
end

-- Test 2.5: call utils functions directly from Luau (not via CallMethod)
print("")
print("--- Test 2.5: utils.formatGreeting called directly from Luau ---")
local utils_direct = NativeMod.utils
print("  utils.formatGreeting('DirectCall') =", utils_direct.formatGreeting("DirectCall"))

-- Test 3: greet should use utils.formatGreeting (via C++ CallMethod)
print("")
print("--- Test 3: greet delegates to @self/utils via C++ CallMethod ---")
local greeting = NativeMod.greet("Yan")
print("  greet('Yan') =", greeting)
if greeting:find("@self/utils") then
    print("  PASS: greet correctly routed through @self/utils")
else
    print("  FAIL: greet did not route through utils (fallback active)")
end

-- Test 4: Native Signal Integration
print("")
print("--- Test 4: Signal & Task Integration ---")
local onTick = NativeMod.onTick
if type(onTick) == "table" and onTick.Connect then
    print("  PASS: onTick signal exposed as table")
else
    print("  FAIL: onTick signal was nil or wrong type (got", type(onTick), ")")
end

-- 4a: the proxy must be read-only (equivalent to table.freeze from Public())
local writeOk = pcall(function()
    onTick.Fire = function() end
end)
print("  write onTick.Fire blocked:", not writeOk)
local rawOk = pcall(function()
    rawset(onTick, "Fire", function() end)
end)
print("  rawset onTick.Fire blocked:", not rawOk)
print("  onTick.Fire is nil (no Fire on proxy):", onTick.Fire == nil)

-- 4b: Connect + Connected + connection read-only protection
local connectFired = false
local conn = onTick:Connect(function(message)
    print("  SIGNAL FIRED! Message:", message)
    connectFired = true
end)
print("  type(conn) =", type(conn))
print("  conn.Connected initially:", conn.Connected)
local connWriteOk = pcall(function()
    conn.Connected = false
end)
print("  write conn.Connected blocked:", not connWriteOk)
local connNewOk = pcall(function()
    conn.Foo = 1
end)
print("  write conn.Foo blocked:", not connNewOk)
print("  getmetatable(conn) protected:", getmetatable(conn) == "connection")
local setMetaOk = pcall(function()
    setmetatable(conn, {})
end)
print("  setmetatable(conn) blocked:", not setMetaOk)

NativeMod.fireOnTick("connect check")
print("  connect callback fired:", connectFired)
print("  conn.Connected after Fire (regular Connect):", conn.Connected)

-- 4c: Once fires only once
local onceCount = 0
local onceConn = onTick:Once(function()
    onceCount = onceCount + 1
end)
NativeMod.fireOnTick("once 1")
NativeMod.fireOnTick("once 2")
print("  Once fired exactly once:", onceCount == 1)
print("  onceConn.Connected after fire:", onceConn.Connected)

-- 4d: Disconnect stops the callback and flips Connected
local disconnectFired = false
local dConn = onTick:Connect(function()
    disconnectFired = true
end)
dConn:Disconnect()
NativeMod.fireOnTick("after disconnect")
print("  Disconnect stopped callback:", not disconnectFired)
print("  dConn.Connected after Disconnect:", dConn.Connected)

-- 4e: Wait resumes with the fire arguments
local task = require("../modules/task")
local waitedArgs = nil
task.spawn(function()
    waitedArgs = { onTick:Wait(2) }
end)
NativeMod.fireOnTick("wait payload")
task.wait(0.05)
print("  Wait returned fire args:", waitedArgs ~= nil and waitedArgs[1] == "wait payload" and #waitedArgs == 1)

-- 4f: Wait times out and returns nil
local timedOut = false
task.spawn(function()
    timedOut = (onTick:Wait(0.05) == nil)
end)
task.wait(0.2)
print("  Wait timed out with nil:", timedOut)

-- 4g: the native delayed task fires the signal after 0.5s
local autoFired = false
onTick:Connect(function(message)
    print("  AUTO SIGNAL FIRED! Message:", message)
    autoFired = true
end)
print("  Waiting 1 second for native delayed signal...")
task.wait(1.0)
if autoFired then
    print("  PASS: Signal was successfully fired by native module delayed task")
else
    print("  FAIL: Signal was not fired in time")
end

-- Test 5: Overhead Benchmarks
print("")
print("--- Test 5: Overhead Benchmarks ---")
local ITERATIONS = 1000000
local t0, t1

-- Benchmark 1: Pure Luau Function
local double = NativeMod.utils.double
t0 = os.clock()
for i = 1, ITERATIONS do
    double(10)
end
t1 = os.clock()
print(string.format("  [Luau -> Luau]        %d calls: %.4f seconds", ITERATIONS, t1 - t0))

-- Benchmark 2: Native C++ Function
local square = NativeMod.square
t0 = os.clock()
for i = 1, ITERATIONS do
    square(10)
end
t1 = os.clock()
print(string.format("  [Luau -> C++]         %d calls: %.4f seconds", ITERATIONS, t1 - t0))

-- Benchmark 3: Native C++ Function that delegates to Luau
local greet = NativeMod.greet
t0 = os.clock()
for i = 1, ITERATIONS do
    greet("Bench")
end
t1 = os.clock()
print(string.format("  [Luau -> C++ -> Luau] %d calls: %.4f seconds", ITERATIONS, t1 - t0))
