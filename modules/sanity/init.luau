--!strict
--!native

local Registry = require("@self/src/Registry")
local Expect = require("@self/src/Expect")
local Runner = require("@self/src/Runner")
local Reporter = require("@self/src/Reporter")
local Discovery = require("@self/src/Discovery")

export type AssertionError = Expect.AssertionError
export type TestResult = Runner.TestResult


--- A function to be executed before or after tests.
export type Hook = () -> ()

--- Expectation matchers.
export type Expect = {
    --- Asserts that the value is strictly equal to the expected value.
    toBe: (
        --- The value to compare against
        expected: any
    ) -> (),
    
    --- Asserts that the value deeply equals the expected value.
    toEqual: (
        --- The value to compare against
        expected: any
    ) -> (),
    
    --- Asserts that the value is truthy (not nil and not false).
    toBeTruthy: () -> (),
    
    --- Asserts that the value is falsy (nil or false).
    toBeFalsy: () -> (),
    
    --- Asserts that the value is nil.
    toBeNil: () -> (),
    
    --- Asserts that the value is not nil.
    toBeDefined: () -> (),
    
    --- Asserts that the typeof() the value matches the expected type.
    toBeType: (
        --- The expected type as a string
        t: string
    ) -> (),
    
    --- Asserts that the number is greater than n.
    toBeGreaterThan: (
        --- The number to compare against
        n: number
    ) -> (),
    
    --- Asserts that the number is less than n.
    toBeLessThan: (
        --- The number to compare against
        n: number
    ) -> (),
    
    --- Asserts that the number is greater than or equal to n.
    toBeGreaterThanOrEqual: (
        --- The number to compare against
        n: number
    ) -> (),
    
    --- Asserts that the number is less than or equal to n.
    toBeLessThanOrEqual: (
        --- The number to compare against
        n: number
    ) -> (),
    
    --- Asserts that the number is close to n within a given precision.
    toBeCloseTo: (
        --- The number to compare against
        n: number,
        --- Optional decimal places of precision (default 2)
        precision: number?
    ) -> (),
    
    --- Asserts that an array or string contains the specified item.
    toContain: (
        --- The item or substring to find
        item: any
    ) -> (),
    
    --- Asserts that an array or string has the specified length.
    toHaveLength: (
        --- The expected length
        n: number
    ) -> (),
    
    --- Asserts that a string matches the given pattern.
    toMatch: (
        --- The Lua string pattern
        pattern: string
    ) -> (),
    
    --- Asserts that the function throws an error when called.
    toThrow: () -> (),
    
    --- Inverts the assertion.
    never: Expect,
}

--- Sanity — a Jest-like test framework for Luau.
--- Auto-discovers `*.test.luau` files or runs inline suites.
--- 
--- Usage:
--- ```lua
--- local sanity = require("@sanity")
--- local describe, it, expect = sanity.describe, sanity.it, sanity.expect
--- 
--- describe("Math Operations", function()
---     it("adds correctly", function()
---         expect(1 + 1).toBe(2)
---     end)
--- end)
--- ```
export type SanityModule = {
    --- Registers a suite (describe block).
    describe: (
        --- The name of the suite
        name: string,
        --- The function containing the tests
        fn: () -> ()
    ) -> (),
    
    --- Registers a single test case.
    it: (
        --- The name of the test
        name: string,
        --- The test function
        fn: () -> ()
    ) -> (),
    
    --- Alias for `it`.
    test: (
        --- The name of the test
        name: string,
        --- The test function
        fn: () -> ()
    ) -> (),

    --- Immediately skips the current test.
    skip: (
        --- The reason for skipping
        reason: string?
    ) -> (),
    
    --- Expects a value to meet certain conditions.
    expect: (
        --- The actual value
        actual: any
    ) -> Expect,
    
    --- Registers a hook that runs once before all tests in the current suite.
    beforeAll: (
        --- The hook function
        fn: Hook
    ) -> (),
    
    --- Registers a hook that runs once after all tests in the current suite.
    afterAll: (
        --- The hook function
        fn: Hook
    ) -> (),
    
    --- Registers a hook that runs before each test in the current suite.
    beforeEach: (
        --- The hook function
        fn: Hook
    ) -> (),
    
    --- Registers a hook that runs after each test in the current suite.
    afterEach: (
        --- The hook function
        fn: Hook
    ) -> (),
    
    --- Runs the whole registered tree.
    run: (
        --- The runner configuration
        config: any,
        --- Optional live progress callbacks
        callbacks: any
    ) -> (any, number, number),
    
    --- Prints the summary and the full block report.
    report: (
        --- The test results array
        results: { any },
        --- Total wall-clock run time
        totalDurationMs: number,
        --- Sum of individual test durations
        testsElapsedMs: number,
        --- The runner configuration
        config: any
    ) -> (),
    
    --- Recursively discovers files matching a pattern under a directory.
    discover: (
        --- The directory to search
        dir: string,
        --- The string pattern to match
        pattern: string
    ) -> { string },
}

local sanity = {
    describe = Registry.describe,
    it = Registry.it,
    test = Registry.test,
    skip = function(msg: string?) error("[SKIPPED] " .. (msg or "")) end,
    expect = Expect.expect,
    beforeAll = Registry.beforeAll,
    afterAll = Registry.afterAll,
    beforeEach = Registry.beforeEach,
    afterEach = Registry.afterEach,
    run = Runner.run,
    report = Reporter.report,
    discover = Discovery.discover,
}

return sanity :: SanityModule