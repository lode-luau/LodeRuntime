-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local sys = require("../../sys")
local FFI = require("../../ffi")
local task = require("../../../modules/task")

local platform = sys.GetPlatform():lower()
local isWindows = platform:find("windows", 1, true) ~= nil
local isMac = platform:find("macos", 1, true) ~= nil
local libc = isWindows and "ucrtbase" or (isMac and "libSystem.B.dylib" or "libc.so.6")

type AbsLibrary = {
    abs: (value: number) -> number,
}

describe("FFI", function()
    it("exposes only the final PascalCase API", function()
        expect(FFI.load).toBe(nil)
        expect(FFI.cdef).toBe(nil)
        expect(FFI.ptr).toBe(nil)
        expect(type(FFI.Load)).toBe("function")
        expect(type(FFI.CDef)).toBe("function")
    end)

    it("registers layouts, descriptors, aliases, and rejects incompatible redeclarations", function()
        FFI.CDef([[typedef struct { int32_t left; uint32_t right; void* pointer; } SanityPoint;]])
        FFI.CDef("typedef uint32_t SanityDword;")
        local pointType = FFI.TypeOf("SanityPoint")
        expect(FFI.SizeOf("SanityPoint")).toBe(16)
        expect(FFI.SizeOf(pointType)).toBe(16)
        expect(FFI.AlignOf(pointType)).toBeGreaterThan(0)
        expect(FFI.OffsetOf("SanityPoint", "right")).toBe(4)
        expect(FFI.OffsetOf(pointType, "pointer")).toBe(8)
        expect(FFI.SizeOf("SanityDword")).toBe(4)
        FFI.CDef("typedef uint32_t SanityDword;")
        expect(pcall(FFI.TypeOf, "NoSuchSanityType")).toBe(false)
        expect(pcall(FFI.SizeOf, {})).toBe(false)
        expect(pcall(FFI.AlignOf, "NoSuchSanityType")).toBe(false)
        expect(pcall(FFI.OffsetOf, "SanityPoint", "missing")).toBe(false)
        expect(pcall(FFI.CDef, [[typedef struct { int64_t left; } SanityPoint;]])).toBe(false)
    end)

    it("creates typed aggregates and retains pointer fields", function()
        FFI.CDef([[typedef struct { int32_t left; uint32_t right; void* pointer; } SanityPoint;]])
        local point = FFI.new("SanityPoint", { left = -7, right = 42 })
        local storage = FFI.new(8)
        point.pointer = storage
        expect(point.left).toBe(-7)
        expect(point.right).toBe(42)
        expect(point.pointer ~= nil).toBe(true)
        expect(FFI.SizeOf(point)).toBe(16)
        expect(pcall(function()
            point.missing = 1
        end)).toBe(false)
        expect(pcall(function()
            point.left = "invalid"
        end)).toBe(false)
        expect(pcall(FFI.new, "NoSuchSanityType")).toBe(false)
        expect(pcall(FFI.new, -1)).toBe(false)
    end)

    it("creates raw struct and union layouts without a registry mutation", function()
        local struct = FFI.Struct("uint8_t first; uint32_t second;")
        local union = FFI.Union("uint32_t integer; float decimal;")
        expect(buffer.len(struct)).toBe(8)
        expect(buffer.len(union)).toBe(4)
        expect(pcall(FFI.Struct, "uint8_t values[];")).toBe(false)
        expect(pcall(FFI.Union, "NoSuchSanityType value;")).toBe(false)
    end)

    it("binds cached ABI functions and preserves a bound function after Close", function()
        local library = FFI.Load(libc, "int abs(int value);")
        expect(library.abs(-17)).toBe(17)
        local typedLibrary = library :: FFI.Library<AbsLibrary>
        expect(typedLibrary.abs(-18)).toBe(18)
        expect(pcall(library.abs)).toBe(false)
        library.Close()
        library.Close()
        expect(library.abs(-2)).toBe(2)
        expect(pcall(FFI.Load, libc, 42)).toBe(false)
        expect(pcall(FFI.Load, libc, nil, { Unknown = true })).toBe(false)
        expect(pcall(FFI.Load, libc, nil, { SearchDllDirectory = "yes" })).toBe(false)
        expect(pcall(FFI.Load, libc, "int SanityMissingSymbol(void);")).toBe(false)
    end)

    it("creates named and anonymous callbacks with idempotent Close", function()
        FFI.CDef("typedef int32_t (*SanityUnary)(int32_t);")
        local descriptor = FFI.TypeOf("SanityUnary")
        local named = FFI.Callback(descriptor, function(value)
            return value + 1
        end)
        expect(named.Pointer ~= nil).toBe(true)
        expect(named.State().closed).toBe(false)
        named.Close()
        named.Close()
        expect(named.State().closed).toBe(true)
        expect(pcall(FFI.Callback, descriptor, 42)).toBe(false)
        expect(pcall(FFI.Callback, descriptor, function() end, { ForeignThread = "invalid" })).toBe(false)

        local anonymous = FFI.Callback("int32_t(int32_t)", function(value)
            return value
        end)
        expect(anonymous.Pointer ~= nil).toBe(true)
        anonymous.Close()
    end)

    it("posts work on the owner runtime thread", function()
        local ran = false
        FFI.Post(function()
            ran = true
        end)
        task.wait(0.01)
        task.wait(0.01)
        expect(ran).toBe(true)
    end)

    it("reports pcall-safe declaration and convention failures", function()
        expect(pcall(FFI.CDef, 42)).toBe(false)
        expect(pcall(FFI.C, 42)).toBe(false)
        expect(pcall(FFI.Post, 42)).toBe(false)
        expect(pcall(FFI.Load, libc, "int __vectorcall abs(int value);")).toBe(false)
    end)
end)
