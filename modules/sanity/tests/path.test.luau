--!strict
--!native

-- Regression tests for the native path module. In particular path.join must
-- stay lexical (no filesystem access): it used to call weakly_canonical,
-- which performed per-component disk I/O on every call.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local path = require("../../path")

local function norm(s: string): string
    -- Native separator is platform-specific; compare in forward-slash form.
    return string.gsub(s, "\\", "/")
end

describe("path.join", function()
    it("joins components lexically and stays relative for relative inputs", function()
        expect(norm(path.join("a", "b", "c"))).toBe("a/b/c")
        expect(norm(path.join("bench", "luau", "run.luau"))).toBe("bench/luau/run.luau")
    end)

    it("collapses . and .. components without touching the filesystem", function()
        expect(norm(path.join("a", "..", "b"))).toBe("b")
        expect(norm(path.join("x", ".", "y"))).toBe("x/y")
        expect(norm(path.join("a", "b", "..", "..", "c"))).toBe("c")
    end)

    it("does not resolve nonexistent paths against the disk", function()
        -- A lexical join must not turn this into an absolute path (the old
        -- weakly_canonical implementation did, with disk I/O per component).
        local joined = norm(path.join("__lode_nonexistent_dir__", "file.txt"))
        expect(joined:sub(1, 1) ~= "/").toBe(true)
        expect(joined:sub(2, 2) ~= ":").toBe(true)
        expect(joined:find("__lode_nonexistent_dir__", 1, true) ~= nil).toBe(true)
    end)

    it("composes with resolve for absolute paths", function()
        local base = path.resolve("@self")
        local joined = path.join(base, "tests", "path.test.luau")
        expect(norm(joined)).toBe(norm(base) .. "/tests/path.test.luau")
    end)

    it("returns an empty string when called with no arguments", function()
        expect(path.join()).toBe("")
    end)
end)
