--!strict

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect
local task = require("../../../temp/task_module")
local fs = require("../../filesystem")

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
end)
