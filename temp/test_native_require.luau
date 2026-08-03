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
