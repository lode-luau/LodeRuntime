--!strict

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect
local task = require("../../../temp/task_module")
local fs = require("../../filesystem")
local native = require("../../../temp/native_module")

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
            fs.readFile("@self/does-not-exist.luau")
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

    it("converts native callback exceptions into Lua errors", function()
        local ok, err = pcall(function() native.throwCpp() end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
    end)

    it("reports malformed native module metadata as a Lua error", function()
        local req: (string) -> any = require
        local ok, err = pcall(function()
            req("../../../temp/invalid_module")
        end)
        expect(ok).toBe(false)
        expect(typeof(err)).toBe("string")
    end)
end)
