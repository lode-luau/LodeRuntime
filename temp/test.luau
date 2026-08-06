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

local largeInteger = NativeMod.makeLargeInteger()
assert(NativeMod.roundTripInteger(largeInteger) == largeInteger, "64-bit integer round-trip failed")
print("[Native Module DLL] 64-bit integer round-trip:", NativeMod.roundTripInteger(largeInteger))

local nativeVector = NativeMod.makeVector()
local roundTripVector = NativeMod.roundTripVector(nativeVector)
assert(type(roundTripVector) == "vector", "vector type round-trip failed")
assert(roundTripVector.x == 1.25 and roundTripVector.y == -2.5 and roundTripVector.z == 3.75, "vector component round-trip failed")
print("[Native Module DLL] vector round-trip:", roundTripVector.x, roundTripVector.y, roundTripVector.z)

local modernObject = NativeMod.modernObject
assert(modernObject // 2 == 21, "__idiv helper failed")
assert(#modernObject == 42, "__len helper failed")
local iterated = {}
for value in modernObject do
    table.insert(iterated, value)
end
assert(#iterated == 3 and tostring(iterated[1]) == "1" and tostring(iterated[3]) == "3", "__iter helper failed")

local sysInfo = NativeMod.getSystemInfo()
print("[Native Module DLL] System Info: Platform =", sysInfo.platform, "| Arch =", sysInfo.arch)

print("[True Multi-Return Require] Primary Exports =", NativeMod)
print("[True Multi-Return Require] Secondary MetaInfo =", MetaInfo, "| Name =", MetaInfo.moduleName, "| Version =", MetaInfo.version)

-- 3. Native C++ Class Binding (Vector3 via ObjectWrap & ClassBuilder 4-Phase Dispatcher)
if NativeMod.Vector3 then
    local v = NativeMod.Vector3.new(3, 4, 0)
    print("[Class Binding ObjectWrap] Vector3 length():", v:length())
    print("[Class Binding ObjectWrap] Vector3 tostring():", tostring(v))
    print("[Class Builder Property Getter] v.x =", v.x, "| v.y =", v.y, "| v.z =", v.z)
    v.x = 10
    print("[Class Builder Property Setter] v.x after set to 10 =", v.x, "| new length =", v:length())
end

-- 4. Native Buffer Zero-Copy Test
if NativeMod.processBuffer then
    local buf = buffer.create(32)
    local testString = "Hello Zero-Copy Buffers!"
    local bytesCopied = NativeMod.processBuffer(buf, testString)
    print("[Zero-Copy Buffer] Copied", bytesCopied, "bytes from string into buffer")
    local readStr = buffer.readstring(buf, 0, bytesCopied)
    print("[Zero-Copy Buffer] Content read back from native buffer:", readStr)
end

print("=== All Integration Tests Passed Successfully! ===")
