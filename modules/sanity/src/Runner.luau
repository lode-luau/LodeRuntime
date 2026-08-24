--!strict

--[[
    Test runner for the Sanity framework.

    Executes the registered suite tree sequentially. Each test runs inside its
    own coroutine (via task.spawn) so async tests (that yield on task.wait or
    filesystem operations) work naturally. The runner yields until each test
    completes, enforcing a per-test timeout.

    Hook semantics (Jest-like):
      - beforeEach runs outermost -> innermost; afterEach runs in reverse.
      - beforeAll of a suite (and its ancestors) runs once, outermost first,
        right before the suite's own part starts; afterAll runs after the
        suite AND all of its descendants have finished (innermost first).
      - Hooks registered on the root level apply to every test.
      - A failing hook fails the affected test/suite instead of being
        swallowed by pcall.
      - beforeAll/afterAll run inside their own coroutine, so hooks that
        yield work as well.
]]

local Registry = require("./Registry")
local Deps = require("./Deps")

export type TestResult = {
    suite: string,
    name: string,
    status: "passed" | "failed" | "skipped",
    error: any,
    durationMs: number,
}

local function runHooks(hooks: { () -> () })
    for _, hook in ipairs(hooks) do
        hook()
    end
end

--- Runs fn inside its own coroutine and pumps until it finishes, enforcing
--- config.timeout. Used for beforeAll/afterAll so async hooks work too.
--- Returns (ok, error).
local function runAsync(deps: any, config: any, label: string, fn: () -> ()): (boolean, any)
    local task = deps.task
    local timeout = config.timeout or 5
    local done = false
    local ok = true
    local err: any = nil

    local co = task.spawn(function()
        local o, e = pcall(function()
            fn()
        end)
        ok = o
        err = e
        done = true
    end)

    local deadline = os.clock() + timeout
    while not done do
        if os.clock() > deadline then
            task.cancel(co)
            return false, label .. " timed out after " .. tostring(timeout) .. "s"
        end
        task.wait(0.005)
    end
    return ok, err
end

--- Runs the beforeAll hooks of `suite` unless they already ran this session.
--- Returns (ran, ok, err): ran=false means they already executed earlier.
local function ensureBeforeAll(deps: any, config: any, suite: any, ranBeforeAll: { [any]: boolean }): (boolean, boolean, any)
    if ranBeforeAll[suite] then
        return false, true, nil
    end
    ranBeforeAll[suite] = true
    if #suite.beforeAll == 0 then
        return true, true, nil
    end
    local ok, err = runAsync(deps, config, "beforeAll", function()
        runHooks(suite.beforeAll)
    end)
    return true, ok, err
end

--- Runs a single test (with inherited beforeEach/afterEach) in a spawned
--- coroutine. Returns a TestResult. Supports async tests and timeouts.
local function runTest(deps: any, suite: any, test: any, config: any, suiteName: string, callbacks: any, ancestors: { any }): TestResult
    local task = deps.task
    local timeout = config.timeout or 5
    local start = os.clock()
    local done = false
    local lastMs = -1

    local result: TestResult = {
        suite = suiteName,
        name = test.name,
        status = "passed",
        error = nil,
        durationMs = 0,
    }

    -- Full beforeEach/afterEach chains including root and every ancestor.
    local beforeChain: { any } = {}
    for _, s in ipairs(ancestors) do
        table.insert(beforeChain, s)
    end
    table.insert(beforeChain, suite)

    local afterChain: { any } = {}
    for i = #beforeChain, 1, -1 do
        table.insert(afterChain, beforeChain[i])
    end

    if callbacks and callbacks.onTestStart then
        callbacks.onTestStart(suiteName, test.name)
    end

    local co = task.spawn(function()
        local ok, err = pcall(function()
            for _, s in ipairs(beforeChain) do
                runHooks(s.beforeEach)
            end
        end)

        if ok then
            local ok2, err2 = pcall(test.fn)
            if ok2 then
                result.status = "passed"
            else
                -- error("[SKIPPED] ...") prefixes the message with the source
                -- location (e.g. "init.luau:214: [SKIPPED] ..."), so match the
                -- marker anywhere in the string rather than only at the start.
                if type(err2) == "string" and string.find(err2, "[SKIPPED]", 1, true) then
                    result.status = "skipped"
                    result.error = err2
                else
                    result.status = "failed"
                    result.error = err2
                end
            end
        else
            result.status = "failed"
            result.error = "beforeEach: " .. tostring(err)
        end

        -- afterEach always runs (innermost first), even after failures, and a
        -- failing afterEach fails the test instead of being silently dropped.
        local okAfter, errAfter = pcall(function()
            for _, s in ipairs(afterChain) do
                runHooks(s.afterEach)
            end
        end)
        if not okAfter and result.status ~= "skipped" then
            result.status = "failed"
            result.error = "afterEach: " .. tostring(errAfter)
        end

        done = true
    end)

    -- Wait for completion, enforcing a timeout.
    local deadline = os.clock() + timeout
    while not done do
        if os.clock() > deadline then
            task.cancel(co)
            result.status = "failed"
            result.error = "Test timed out after " .. tostring(timeout) .. "s"
            done = true
            break
        end

        if callbacks and callbacks.onTestProgress then
            local ms = math.floor((os.clock() - start) * 1000)
            if ms ~= lastMs then
                lastMs = ms
                callbacks.onTestProgress(suiteName, test.name, ms)
            end
        end

        task.wait(0.005)
    end

    result.durationMs = math.floor((os.clock() - start) * 1000 + 0.5)
    if callbacks and callbacks.onTestEnd then
        callbacks.onTestEnd(result)
    end
    return result
end

--- Runs a suite: beforeAll chain -> tests -> children -> own afterAll.
--- Parent afterAll is deferred until every descendant finished.
local function runSuite(deps: any, suite: any, results: { TestResult }, config: any, prefix: string, callbacks: any, ancestors: { any }, ranBeforeAll: { [any]: boolean })
    local fullName = prefix .. suite.name .. " > "

    -- Ensure this suite and every ancestor ran its beforeAll (outermost first).
    local chain: { any } = {}
    for _, s in ipairs(ancestors) do
        table.insert(chain, s)
    end
    table.insert(chain, suite)

    for _, s in ipairs(chain) do
        local ran, ok, err = ensureBeforeAll(deps, config, s, ranBeforeAll)
        if ran and not ok then
            table.insert(results, {
                suite = prefix .. s.name .. " > ",
                name = "(beforeAll)",
                status = "failed",
                error = err,
                durationMs = 0,
            })
            -- Still run this suite's afterAll to release resources.
            pcall(function()
                runHooks(s.afterAll)
            end)
            return
        end
    end

    for _, test in ipairs(suite.tests) do
        local r = runTest(deps, suite, test, config, fullName, callbacks, ancestors)
        table.insert(results, r)
    end

    -- Children run BEFORE this suite's own afterAll (Jest deferral).
    for _, child in ipairs(suite.children) do
        runSuite(deps, child, results, config, fullName, callbacks, chain, ranBeforeAll)
    end

    if #suite.afterAll > 0 then
        local ok, err = runAsync(deps, config, "afterAll", function()
            runHooks(suite.afterAll)
        end)
        if not ok then
            table.insert(results, {
                suite = fullName,
                name = "(afterAll)",
                status = "failed",
                error = err,
                durationMs = 0,
            })
        end
    end
end

--- Runs the whole registered tree. Returns (results, totalDurationMs, testsElapsedMs).
--- Root-level hooks (registered outside any describe) apply to everything.
local function run(config: any, callbacks: any): (any, number, number)
    local results: { TestResult } = {}
    local root = Registry.getRoot()
    local start = os.clock()

    local ranBeforeAll: { [any]: boolean } = {}

    -- Root beforeAll (once, applies to top-level tests and all suites).
    do
        local _, ok, err = ensureBeforeAll(Deps, config, root, ranBeforeAll)
        if not ok then
            table.insert(results, {
                suite = "",
                name = "(beforeAll)",
                status = "failed",
                error = err,
                durationMs = 0,
            })
            return results, (os.clock() - start) * 1000, 0
        end
    end

    local rootAncestors: { any } = { root }

    -- Top-level tests (registered outside any describe)
    for _, test in ipairs(root.tests) do
        local r = runTest(Deps, root, test, config, "", callbacks, {})
        table.insert(results, r)
    end

    -- Suites
    for _, suite in ipairs(root.children) do
        runSuite(Deps, suite, results, config, "", callbacks, rootAncestors, ranBeforeAll)
    end

    -- Root afterAll last, after everything else finished.
    if #root.afterAll > 0 then
        local ok, err = runAsync(Deps, config, "afterAll", function()
            runHooks(root.afterAll)
        end)
        if not ok then
            table.insert(results, {
                suite = "",
                name = "(afterAll)",
                status = "failed",
                error = err,
                durationMs = 0,
            })
        end
    end

    local total = (os.clock() - start) * 1000

    local testsElapsed = 0
    for _, r in ipairs(results) do
        testsElapsed += r.durationMs
    end

    return results, total, testsElapsed
end

return {
    run = run,
}