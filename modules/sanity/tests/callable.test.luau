
-- Regression tests for callable values (__call metamethod) through the C++ Value call API.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local native = require("../../../tests/native/native_module")

describe("callable values", function()
    it("calls a table with __call through the native Value API", function()
        local callable = setmetatable({}, {
            __call = function(self, x, y)
                return x * 10 + y
            end,
        })
        expect(native.callCallable(callable, 4, 2)).toBe(42)
    end)

    it("rejects non-callable values with an error", function()
        local result = native.callNonCallable(42)
        expect(result.ok).toBe(false)
        expect(#result.error > 0).toBe(true)
    end)
end)
