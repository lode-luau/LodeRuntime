--!strict

--[[
    Shared test registry for the Sanity test framework.

    All test files and the runner share this singleton through the require
    cache (they all resolve to the same absolute path). This module only
    handles *registration* of suites, tests and hooks. Execution is done by
    the Runner module.
]]

export type Hook = () -> ()

export type Test = {
    name: string,
    fn: () -> (),
}

export type Suite = {
    name: string,
    tests: { Test },
    children: { Suite },
    beforeAll: { Hook },
    afterAll: { Hook },
    beforeEach: { Hook },
    afterEach: { Hook },
}

local Registry = {}

local root: Suite = {
    name = "<root>",
    tests = {},
    children = {},
    beforeAll = {},
    afterAll = {},
    beforeEach = {},
    afterEach = {},
}

local stack: { Suite } = { root }

local function currentSuite(): Suite
    return stack[#stack]
end

--- Registers a suite (describe block). The callback runs immediately and
--- registers nested tests/hooks/describes.
function Registry.describe(name: string, fn: () -> ())
    local suite: Suite = {
        name = name,
        tests = {},
        children = {},
        beforeAll = {},
        afterAll = {},
        beforeEach = {},
        afterEach = {},
    }

    local parent = currentSuite()
    table.insert(parent.children, suite)
    table.insert(stack, suite)

    local ok, err = pcall(function()
        fn()
    end)
    table.remove(stack)

    if not ok then
        error("describe '" .. name .. "': " .. tostring(err))
    end
end

--- Registers a single test case.
function Registry.it(name: string, fn: () -> ())
    local suite = currentSuite()
    table.insert(suite.tests, { name = name, fn = fn })
end

--- Alias for `it`.
Registry.test = Registry.it

--- Registers a hook that runs once before all tests in the current suite.
function Registry.beforeAll(fn: Hook)
    table.insert(currentSuite().beforeAll, fn)
end

--- Registers a hook that runs once after all tests in the current suite.
function Registry.afterAll(fn: Hook)
    table.insert(currentSuite().afterAll, fn)
end

--- Registers a hook that runs before each test in the current suite.
function Registry.beforeEach(fn: Hook)
    table.insert(currentSuite().beforeEach, fn)
end

--- Registers a hook that runs after each test in the current suite.
function Registry.afterEach(fn: Hook)
    table.insert(currentSuite().afterEach, fn)
end

--- Returns the root suite (the whole registered tree).
function Registry.getRoot(): Suite
    return root
end

--- Clears all registered tests/hooks. Useful for re-running a suite.
function Registry.reset()
    root = {
        name = "<root>",
        tests = {},
        children = {},
        beforeAll = {},
        afterAll = {},
        beforeEach = {},
        afterEach = {},
    }
    stack = { root }
end

return Registry