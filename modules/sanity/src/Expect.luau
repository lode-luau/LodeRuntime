--!strict

--[[
    Expectation/assertion library for the Sanity test framework.

    `expect(value)` returns an object with matchers (toBe, toEqual, ...) plus
    a `.not` negation wrapper. On failure, matchers throw a tagged table
    `{ kind = "AssertionError", message, expected, received }` so the Reporter
    can format expected vs received nicely.
]]

export type AssertionError = {
    kind: "AssertionError",
    message: string,
    expected: any,
    received: any,
}

local function fail(message: string, expected: any, received: any)
    error({ kind = "AssertionError", message = message, expected = expected, received = received })
end

--- Recursive deep equality with cycle protection.
local function deepEqual(a: any, b: any, seen: { [any]: boolean }?): boolean
    if a == b then
        return true
    end
    if typeof(a) ~= typeof(b) then
        return false
    end
    if typeof(a) ~= "table" then
        return false
    end

    local vis = seen or {}
    if vis[a] then
        return true
    end
    vis[a] = true

    for k, v in pairs(a) do
        if not deepEqual(v, b[k], vis) then
            return false
        end
    end
    for k in pairs(b) do
        if a[k] == nil then
            return false
        end
    end

    return true
end

--- Human-readable representation of a value (bounded depth).
local function formatValue(v: any, depth: number?): string
    local d = depth or 0
    if v == nil then
        return "nil"
    end

    local t = typeof(v)
    if t == "string" then
        return string.format("%q", v)
    elseif t == "number" or t == "boolean" then
        return tostring(v)
    elseif t == "table" then
        if d > 3 then
            return "{...}"
        end
        local parts = {}
        for k, val in pairs(v) do
            table.insert(parts, tostring(k) .. "=" .. formatValue(val, d + 1))
        end
        return "{" .. table.concat(parts, ", ") .. "}"
    else
        return tostring(v)
    end
end

export type Expect = {
    toBe: (expected: any) -> (),
    toEqual: (expected: any) -> (),
    toBeTruthy: () -> (),
    toBeFalsy: () -> (),
    toBeNil: () -> (),
    toBeDefined: () -> (),
    toBeType: (t: string) -> (),
    toBeGreaterThan: (n: number) -> (),
    toBeLessThan: (n: number) -> (),
    toBeGreaterThanOrEqual: (n: number) -> (),
    toBeLessThanOrEqual: (n: number) -> (),
    toBeCloseTo: (n: number, precision: number?) -> (),
    toContain: (item: any) -> (),
    toHaveLength: (n: number) -> (),
    toMatch: (pattern: string) -> (),
    toThrow: () -> (),
    never: Expect,
}

--- Builds a matcher object for a given actual value and negation flag.
--- Does NOT wire up `.never` (that is done by `makeExpect` to avoid recursion).
local function buildMatchers(actual: any, negate: boolean): Expect
    local self = {} :: any

    local function check(passed: boolean, message: string, expected: any)
        local ok = negate and (not passed) or passed
        if not ok then
            fail(message, expected, actual)
        end
    end

    self.toBe = function(expected: any)
        check(actual == expected, "expected " .. formatValue(actual) .. " to be " .. formatValue(expected), expected)
    end

    self.toEqual = function(expected: any)
        check(deepEqual(actual, expected), "expected " .. formatValue(actual) .. " to deeply equal " .. formatValue(expected), expected)
    end

    self.toBeTruthy = function()
        check(actual ~= nil and actual ~= false, "expected " .. formatValue(actual) .. " to be truthy", true)
    end

    self.toBeFalsy = function()
        check(actual == nil or actual == false, "expected " .. formatValue(actual) .. " to be falsy", false)
    end

    self.toBeNil = function()
        check(actual == nil, "expected " .. formatValue(actual) .. " to be nil", nil)
    end

    self.toBeDefined = function()
        check(actual ~= nil, "expected value to be defined (not nil)", nil)
    end

    self.toBeType = function(t: string)
        check(typeof(actual) == t, "expected " .. formatValue(actual) .. " to be of type " .. t, t)
    end

    self.toBeGreaterThan = function(n: number)
        check(typeof(actual) == "number" and actual > n, "expected " .. formatValue(actual) .. " to be greater than " .. tostring(n), n)
    end

    self.toBeLessThan = function(n: number)
        check(typeof(actual) == "number" and actual < n, "expected " .. formatValue(actual) .. " to be less than " .. tostring(n), n)
    end

    self.toBeGreaterThanOrEqual = function(n: number)
        check(typeof(actual) == "number" and actual >= n, "expected " .. formatValue(actual) .. " to be >= " .. tostring(n), n)
    end

    self.toBeLessThanOrEqual = function(n: number)
        check(typeof(actual) == "number" and actual <= n, "expected " .. formatValue(actual) .. " to be <= " .. tostring(n), n)
    end

    self.toBeCloseTo = function(n: number, precision: number?)
        local eps = 10 ^ -(precision or 2)
        check(typeof(actual) == "number" and math.abs(actual - n) <= eps, "expected " .. formatValue(actual) .. " to be close to " .. tostring(n), n)
    end

    self.toContain = function(item: any)
        local found = false
        if typeof(actual) == "string" then
            found = string.find(actual, item, 1, true) ~= nil
        elseif typeof(actual) == "table" then
            for _, v in pairs(actual) do
                if v == item then
                    found = true
                    break
                end
            end
        end
        check(found, "expected " .. formatValue(actual) .. " to contain " .. formatValue(item), item)
    end

    self.toHaveLength = function(n: number)
        local len = 0
        if typeof(actual) == "string" then
            len = #actual
        elseif typeof(actual) == "table" then
            len = #actual
        end
        check(len == n, "expected length " .. tostring(len) .. " to be " .. tostring(n), n)
    end

    self.toMatch = function(pattern: string)
        local matched = typeof(actual) == "string" and string.match(actual, pattern) ~= nil
        check(matched, "expected " .. formatValue(actual) .. " to match pattern " .. pattern, pattern)
    end

    self.toThrow = function()
        local threw = false
        if typeof(actual) == "function" then
            local ok = pcall(actual)
            threw = not ok
        end
        check(threw, "expected function to throw", nil)
    end

    return self :: Expect
end

--- Creates an Expect object for the given actual value.
local function makeExpect(actual: any, negate: boolean): Expect
    local self = buildMatchers(actual, negate)
    self.never = buildMatchers(actual, not negate)
    self.never.never = self
    return self
end

local function expect(actual: any): Expect
    return makeExpect(actual, false)
end

return {
    expect = expect,
    deepEqual = deepEqual,
    formatValue = formatValue,
}