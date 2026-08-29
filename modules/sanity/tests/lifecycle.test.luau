
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect
local task = require("../../../modules/task")
local fs = require("../../filesystem")
local native = require("../../../tests/native/native_module")

describe("runtime lifecycle and error handling", function()
    it("preserves non-string Lua errors through protected calls", function()
        local ok, err = pcall(function()
            error({ reason = "structured error" })
        end)

        expect(ok).toBe(false)
        expect(typeof(err)).toBe("table")
    end)

    it("does not execute a cleared timeout", function()
        local called = false
        local timerId = task.setTimeout(function()
            called = true
        end, 10)

        task.clearTimeout(timerId)
        task.wait(0.05)
        expect(called).toBe(false)
    end)

    it("reports failed filesystem operations without corrupting the VM", function()
        local ok, err = pcall(function()
            fs.ReadFile("@self/does-not-exist.luau")
        end)

        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
    end)

    it("moves reference values when resuming a coroutine", function()
        local original = { marker = "reference" }
        local roundTrip = native.roundTripReference(original)
        expect(roundTrip).toEqual(original)
    end)

    it("preserves embedded null bytes in native string conversions", function()
        local value = native.roundTripString("prefix" .. string.char(0) .. "suffix")
        expect(#value).toBe(13)
        expect(string.byte(value, 7)).toBe(0)
        expect(string.sub(value, 8)).toBe("suffix")
    end)

    it("rejects extreme buffer offsets without accessing memory", function()
        local buffer = buffer.create(4)
        expect(native.bufferBoundsProbe(buffer)).toBe(0)
    end)

    it("copies overlapping buffer ranges safely", function()
        expect(native.bufferOverlapProbe()).toBe("AABC")
    end)

    it("rejects invalid timer durations", function()
        local invalid = { -1, math.huge, -math.huge, 0 / 0 }
        for _, duration in ipairs(invalid) do
            local ok = pcall(function()
                task.setTimeout(function() end, duration)
            end)
            expect(ok).toBe(false)
        end
    end)

    it("converts native callback exceptions into Lua errors", function()
        local ok, err = pcall(function() native.throwCpp() end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
    end)

    it("uses the direct no-yield native path and rejects accidental yields", function()
        expect(native.fastNoYield(9)).toBe(9)
        local ok = pcall(native.yieldFromNoYield)
        expect(ok).toBe(false)
    end)

    it("contains exceptions from raw exports and metamethod callbacks", function()
        local okExport, exportError = pcall(function() native.throwRaw() end)
        expect(okExport).toBe(false)
        expect(typeof(exportError)).toBe("string")

        local okMetatable, metatableError = pcall(function()
            local _ = native.throwingObject.missing
        end)
        expect(okMetatable).toBe(false)
        expect(typeof(metatableError)).toBe("string")
    end)

    it("reports malformed native module metadata as a Lua error", function()
        local req: (string) -> any = require
        local ok, err = pcall(function()
            req("../../../tests/native/invalid_module")
        end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
    end)

    it("rejects native libraries outside the module directory", function()
        local req: (string) -> any = require
        local ok, err = pcall(function()
            req("../../../tests/native/path_escape_module")
        end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
        -- The loader may reject this manifest during validation or during
        -- resolution; both paths must refuse the escaped native library.
        expect(#tostring(err) > 0).toBe(true)
    end)

    it("rejects native packages without a library for the current target", function()
        local req: (string) -> any = require
        local ok, err = pcall(function()
            req("../../../tests/native/missing_platform_module")
        end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
        expect(string.find(tostring(err), "has no compiled artifact for the current platform/architecture", 1, true) ~= nil).toBe(true)
        expect(string.find(tostring(err), "type-only and not executed", 1, true) ~= nil).toBe(true)
    end)
end)
