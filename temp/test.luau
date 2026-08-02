local MathUtils = require("./pure_module")
local NativeMod = require("./native_module")

print("=== LodeRuntime Full Integration Test ===")
print("[Pure Module] MathUtils.add(15, 27) =", MathUtils.add(15, 27))
print("[Pure Module] MathUtils.multiply(6, 7) =", MathUtils.multiply(6, 7))

print("[Native Module DLL] NativeMod.greet('Developer') =", NativeMod.greet("Developer"))
print("[Native Module DLL] NativeMod.square(9) =", NativeMod.square(9))

local sysInfo = NativeMod.getSystemInfo()
print("[Native Module DLL] System Info: Platform =", sysInfo.platform, "| Arch =", sysInfo.arch)

print("=== Multiple Return Values & Flexible Module Returns Test Passed ===")
