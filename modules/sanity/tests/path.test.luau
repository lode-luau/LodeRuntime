
-- Regression tests for the native path module. Both path.join and
-- path.resolve must stay lexical (no filesystem access): they used to call
-- weakly_canonical, which performed per-component disk I/O on every call.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local path = require("../../filesystem/path")

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

describe("path.resolve", function()
    it("returns an absolute path", function()
        local n = norm(path.resolve("a", "b"))
        expect(n:sub(1, 1) == "/" or n:sub(2, 2) == ":").toBe(true)
    end)

    it("resolves relative paths against the caller module directory", function()
        -- path.test.luau lives in modules/sanity/tests
        local n = norm(path.resolve("init"))
        expect(n:find("modules/sanity/tests/init", 1, true) ~= nil).toBe(true)
    end)

    it("resolves @self to the caller module directory", function()
        local n = norm(path.resolve("@self"))
        expect(n:find("modules/sanity/tests", 1, true) ~= nil).toBe(true)
    end)

    it("collapses . and .. components lexically", function()
        local base = norm(path.resolve("@self"))
        expect(norm(path.resolve("@self", "a", "..", "b"))).toBe(base .. "/b")
    end)

    it("does not touch the filesystem for nonexistent paths", function()
        local n = norm(path.resolve("@self", "__lode_nonexistent_dir__", "file.txt"))
        expect(n:find("__lode_nonexistent_dir__", 1, true) ~= nil).toBe(true)
    end)

    it("replaces the base when a later argument is absolute", function()
        local absBase = path.resolve("@self")
        local n = norm(path.resolve("other", absBase, "w.txt"))
        expect(n).toBe(norm(path.join(absBase, "w.txt")))
    end)
end)

describe("path error categorization", function()
    it("rejects a non-string argument in join", function()
        local ok, err = pcall(function()
            (path.join :: any)("a", 123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path.join: path must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string argument in resolve", function()
        local ok, err = pcall(function()
            (path.resolve :: any)(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path.resolve: path must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string argument in basename", function()
        local ok, err = pcall(function()
            (path.basename :: any)(true)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path.basename: path must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string argument in dirname", function()
        local ok, err = pcall(function()
            (path.dirname :: any)({})
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path.dirname: path must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string argument in extname", function()
        local ok, err = pcall(function()
            (path.extname :: any)(42)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path.extname: path must be a string", 1, true) ~= nil).toBe(true)
    end)
end)

describe("path additions", function()
    it("isAbsolute performs a lexical check", function()
        -- The native path module follows the host filesystem's lexical rules.
        local isWindowsStyle = path.isAbsolute("C:/x/y")
        if isWindowsStyle then
            expect(path.isAbsolute("C:/x/y")).toBe(true)
            -- On Windows, a rooted path without a drive is not absolute.
            expect(path.isAbsolute("/usr/local")).toBe(false)
            expect(path.isAbsolute("\\\\server\\\\share")).toBe(true)
        else
            expect(path.isAbsolute("C:/x/y")).toBe(false)
            expect(path.isAbsolute("/usr/local")).toBe(true)
            expect(path.isAbsolute("\\\\server\\\\share")).toBe(false)
        end
        expect(path.isAbsolute("relative/x")).toBe(false)
        expect(pcall(function() return path.isAbsolute(123) end)).toBe(false)
    end)

    it("normalize collapses dots and duplicate separators lexically", function()
        expect(path.normalize("a//b/./c/../d")).toBe(path.join("a", "b", "d"))
        expect(path.normalize("a/../..")).toBe("..")
        expect(pcall(function() return path.normalize(5) end)).toBe(false)
    end)
end)
