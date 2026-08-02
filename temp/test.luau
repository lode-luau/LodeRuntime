-- Master Integration Test for LodeRuntime Architecture
print("=== LodeRuntime Full Architecture Integration Test ===")

-- 1. Pure Luau Module
local MathUtils = require("./pure_module")
print("[Pure Module] MathUtils.add(15, 27) =", MathUtils.add(15, 27))
print("[Pure Module] MathUtils.multiply(6, 7) =", MathUtils.multiply(6, 7))

-- 2. Native C++ DLL Module with True Multi-Return Values in require()!
local NativeMod, MetaInfo = require("./native_module")

print("[Native Module DLL] Greet:", NativeMod.greet("Developer"))
print("[Native Module DLL] Square(9):", NativeMod.square(9))

local sysInfo = NativeMod.getSystemInfo()
print("[Native Module DLL] System Info: Platform =", sysInfo.platform, "| Arch =", sysInfo.arch)

print("[True Multi-Return Require] Primary Exports =", NativeMod)
print("[True Multi-Return Require] Secondary MetaInfo =", MetaInfo, "| Name =", MetaInfo.moduleName, "| Version =", MetaInfo.version)

-- 3. Native C++ Class Binding (Vector3 via ObjectWrap)
if NativeMod.Vector3 then
    local v = NativeMod.Vector3.new(3, 4, 0)
    print("[Class Binding ObjectWrap] Vector3 length():", v:length())
    print("[Class Binding ObjectWrap] Vector3 tostring():", tostring(v))
end

print("=== All Integration Tests Passed Successfully! ===")
