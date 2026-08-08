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
    print("  PASS: onTick signal exposed")
    local fired = false
    onTick:Connect(function(message)
        print("  SIGNAL FIRED! Message:", message)
        fired = true
    end)
    
    print("  Waiting 1 second for signal...")
    local task = require("../modules/task")
    task.wait(1.0)
    
    if fired then
        print("  PASS: Signal was successfully fired by native module delayed task")
    else
        print("  FAIL: Signal was not fired in time")
    end
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
