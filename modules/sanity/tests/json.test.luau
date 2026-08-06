--!strict
--!native

-- Regression tests for the native json module (Lode::Json).

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local json = require("../../json")

describe("json.parse", function()
    it("parses scalar values", function()
        expect(json.parse("123")).toBe(123)
        expect(json.parse("-5")).toBe(-5)
        expect(json.parse("1.5")).toBe(1.5)
        expect(json.parse("1e2")).toBe(100)
        expect(json.parse("true")).toBe(true)
        expect(json.parse("false")).toBe(false)
        expect(json.parse("null")).toBeNil()
        expect(json.parse('"hi"')).toBe("hi")
        expect(json.parse('"a\\nb\\u00e9"')).toBe("a\nbé")
    end)

    it("parses nested objects and arrays", function()
        local v = json.parse('{"a": 1, "b": [true, null, "x"], "c": {"d": 2.5}}')
        expect(v.a).toBe(1)
        expect(v.b[1]).toBe(true)
        expect(v.b[2]).toBeNil()
        expect(v.b[3]).toBe("x")
        expect(v.c.d).toBe(2.5)
    end)

    it("handles large unsigned integers without losing the key", function()
        local v = json.parse('{"u": 18446744073709551615}')
        expect(v.u).toBeDefined()
    end)

    it("rejects trailing data and malformed input", function()
        expect(pcall(function() json.parse('{"a":1} extra') end)).toBe(false)
        expect(pcall(function() json.parse('{"a":}') end)).toBe(false)
        expect(pcall(function() json.parse("[1,2") end)).toBe(false)
        expect(pcall(function() json.parse('"\\ud800"') end)).toBe(false)
    end)

    it("enforces the depth limit", function()
        local ok, err = pcall(function()
            json.parse("[" .. string.rep("[", 600) .. string.rep("]", 600) .. "]")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("depth", 1, true) ~= nil).toBe(true)
    end)

    it("enforces the node limit", function()
        local ok, err = pcall(function()
            json.parse("[" .. string.rep("0,", 2000) .. "]", 512, 1000)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("elements", 1, true) ~= nil).toBe(true)
    end)
end)

describe("json.stringify", function()
    it("stringifies scalars", function()
        expect(json.stringify(nil)).toBe("null")
        expect(json.stringify(true)).toBe("true")
        expect(json.stringify(1.5)).toBe("1.5")
        expect(json.stringify(-7)).toBe("-7")
        expect(json.stringify("x")).toBe('"x"')
    end)

    it("stringifies tables", function()
        expect(json.stringify({})).toBe("{}")
        expect(json.stringify({ a = 1, b = { 2, 3 } })).toBe('{"a":1,"b":[2,3]}')
        expect(json.stringify({ 1, 2, name = "x" })).toBe('{"1":1,"2":2,"name":"x"}')
    end)

    it("round-trips parsed documents", function()
        local rt = json.parse('{"a":[1,2,3],"b":"x"}')
        expect(json.stringify(rt)).toBe('{"a":[1,2,3],"b":"x"}')
    end)

    it("emits pretty output when requested", function()
        expect(json.stringify({ a = 1 }, true)).toBe("{\n    \"a\": 1\n}")
    end)

    it("rejects unencodable values", function()
        expect(pcall(function() json.stringify({ 1, nil, 3 }) end)).toBe(false)
        expect(pcall(function() json.stringify(1 / 0) end)).toBe(false)
        expect(pcall(function() json.stringify(0 / 0) end)).toBe(false)
        expect(pcall(function() json.stringify(function() end) end)).toBe(false)
    end)

    it("enforces limits while stringifying", function()
        local deepTable = {}
        local cur = deepTable
        for _ = 1, 600 do
            local next = {}
            cur[1] = next
            cur = next
        end
        local okDeep, deepErr = pcall(function() json.stringify(deepTable) end)
        expect(okDeep).toBe(false)
        expect(tostring(deepErr):find("depth", 1, true) ~= nil).toBe(true)

        local wideTable = {}
        for i = 1, 2000 do
            wideTable[i] = 0
        end
        local okWide, wideErr = pcall(function() json.stringify(wideTable, false, 512, 1000) end)
        expect(okWide).toBe(false)
        expect(tostring(wideErr):find("elements", 1, true) ~= nil).toBe(true)
    end)
end)
