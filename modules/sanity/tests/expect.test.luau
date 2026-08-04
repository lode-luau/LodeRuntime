--!strict
--!native

-- Self-tests for the Sanity `expect` matchers.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

describe("expect matchers", function()
    it("toBe with primitives", function()
        expect(1).toBe(1)
        expect("hello").toBe("hello")
        expect(true).toBe(true)
        expect(nil).toBe(nil)
    end)

    it("toBe with .never", function()
        expect(1).never.toBe(2)
        expect("a").never.toBe("b")
    end)

    it("toEqual deep equality", function()
        expect({ a = 1, b = { c = 2 } }).toEqual({ a = 1, b = { c = 2 } })
        expect({ 1, 2, 3 }).toEqual({ 1, 2, 3 })
    end)

    it("toEqual with .never", function()
        expect({ a = 1 }).never.toEqual({ a = 2 })
    end)

    it("toBeTruthy / toBeFalsy", function()
        expect(1).toBeTruthy()
        expect("x").toBeTruthy()
        expect(false).toBeFalsy()
        expect(nil).toBeFalsy()
    end)

    it("toBeNil / toBeDefined", function()
        expect(nil).toBeNil()
        expect(0).toBeDefined()
    end)

    it("toBeType", function()
        expect("str").toBeType("string")
        expect(42).toBeType("number")
        expect({}).toBeType("table")
    end)

    it("numeric comparisons", function()
        expect(5).toBeGreaterThan(3)
        expect(3).toBeLessThan(5)
        expect(5).toBeGreaterThanOrEqual(5)
        expect(5).toBeLessThanOrEqual(5)
    end)

    it("toBeCloseTo", function()
        expect(0.1 + 0.2).toBeCloseTo(0.3)
    end)

    it("toContain", function()
        expect("hello world").toContain("world")
        expect({ 1, 2, 3 }).toContain(2)
    end)

    it("toHaveLength", function()
        expect("hello").toHaveLength(5)
        expect({ 1, 2, 3 }).toHaveLength(3)
    end)

    it("toMatch", function()
        expect("hello123").toMatch("%d+")
    end)

    it("toThrow", function()
        expect(function()
            error("boom")
        end).toThrow()
    end)
end)