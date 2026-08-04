--!strict
--!native

--[[
    Sanity — a Jest-like test framework for Luau.

    Pure-Luau module that builds on three native modules:
      * stdio      -> output / reporting
      * task       -> async execution
      * filesystem -> test discovery

    Usage in a test file:
        local sanity = require("...path to sanity...")
        local describe = sanity.describe
        local it = sanity.it
        local expect = sanity.expect

        describe("math", function()
            it("adds", function()
                expect(1 + 1).toBe(2)
            end)
        end)

    Run via the CLI entry point (run.luau) which auto-discovers *.test.luau
    files, or call sanity.run() directly.
]]

local Registry = require("@self/src/Registry")
local Expect = require("@self/src/Expect")
local Runner = require("@self/src/Runner")
local Reporter = require("@self/src/Reporter")
local Discovery = require("@self/src/Discovery")

export type AssertionError = Expect.AssertionError
export type TestResult = Runner.TestResult

local sanity = {
    describe = Registry.describe,
    it = Registry.it,
    test = Registry.test,
    expect = Expect.expect,
    beforeAll = Registry.beforeAll,
    afterAll = Registry.afterAll,
    beforeEach = Registry.beforeEach,
    afterEach = Registry.afterEach,
    run = Runner.run,
    report = Reporter.report,
    discover = Discovery.discover,
}

return sanity