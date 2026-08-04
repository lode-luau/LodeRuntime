--!strict
--!native

-- Self-tests for hooks (beforeAll/afterAll/beforeEach/afterEach) and nesting.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect
local beforeAll = sanity.beforeAll
local afterAll = sanity.afterAll
local beforeEach = sanity.beforeEach
local afterEach = sanity.afterEach

describe("hooks", function()
    local log: { string } = {}

    beforeAll(function()
        table.insert(log, "beforeAll")
    end)

    afterAll(function()
        table.insert(log, "afterAll")
    end)

    beforeEach(function()
        table.insert(log, "beforeEach")
    end)

    afterEach(function()
        table.insert(log, "afterEach")
    end)

    it("first test runs after beforeAll + beforeEach", function()
        table.insert(log, "test1")
        -- log = [beforeAll, beforeEach, test1]
        expect(#log).toBe(3)
    end)

    it("second test sees accumulated hook log", function()
        table.insert(log, "test2")
        -- log = [beforeAll, beforeEach, test1, afterEach, beforeEach, test2]
        expect(#log).toBe(6)
    end)
end)

describe("nested suites", function()
    describe("inner", function()
        it("runs nested tests", function()
            expect(2 + 2).toBe(4)
        end)
    end)

    it("runs sibling tests", function()
        expect("nested").toMatch("nest")
    end)
end)