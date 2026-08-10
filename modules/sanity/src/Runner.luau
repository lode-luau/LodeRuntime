--!strict

--[[
    Test runner for the Sanity framework.

    Executes the registered suite tree sequentially. Each test runs inside its
    own coroutine (via task.spawn) so async tests (that yield on task.wait or
    filesystem operations) work naturally. The runner yields until each test
    completes, enforcing a per-test timeout.
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

--- Runs a single test (with beforeEach/afterEach) in a spawned coroutine.
--- Returns a TestResult. Supports async tests and a per-test timeout.
--- When `callbacks` is provided, it is notified as the test starts, periodically
--- while it runs (onTestProgress), and when it finishes (onTestEnd).
local function runTest(deps: any, suite: any, test: any, config: any, suiteName: string, callbacks: any): TestResult
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

    if callbacks and callbacks.onTestStart then
        callbacks.onTestStart(suiteName, test.name)
    end

    local co = task.spawn(function()
        local ok, err = pcall(function()
            runHooks(suite.beforeEach)
        end)

        if ok then
            local ok2, err2 = pcall(test.fn)
            if ok2 then
                result.status = "passed"
            else
                -- `error("[SKIPPED] ...")` prefixes the message with the source
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

        -- afterEach always runs, even on failure
        pcall(function()
            runHooks(suite.afterEach)
        end)

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

        task.wait()
    end

    result.durationMs = math.floor((os.clock() - start) * 1000 + 0.5)
    if callbacks and callbacks.onTestEnd then
        callbacks.onTestEnd(result)
    end
    return result
end

--- Runs a suite: beforeAll -> tests -> children -> afterAll.
local function runSuite(deps: any, suite: any, results: { TestResult }, config: any, prefix: string, callbacks: any)
    local fullName = prefix .. suite.name .. " > "

    local ok, err = pcall(function()
        runHooks(suite.beforeAll)
    end)
    if not ok then
        table.insert(results, {
            suite = fullName,
            name = "(beforeAll)",
            status = "failed",
            error = err,
            durationMs = 0,
        })
        return
    end

    for _, test in ipairs(suite.tests) do
        local r = runTest(deps, suite, test, config, fullName, callbacks)
        table.insert(results, r)
    end

    for _, child in ipairs(suite.children) do
        runSuite(deps, child, results, config, fullName, callbacks)
    end

    pcall(function()
        runHooks(suite.afterAll)
    end)
end

--- Runs the whole registered tree. Returns (results, totalDurationMs, testsElapsedMs).
--- `totalDurationMs` is wall-clock time including runner overhead (hook setup,
--- coroutine spawn, pump loop); `testsElapsedMs` is the sum of the individual
--- test durations. `callbacks` (optional) streams live progress:
---   onTestStart(suite, name) / onTestProgress(suite, name, ms) / onTestEnd(result)
local function run(config: any, callbacks: any): (any, number, number)
    local results: { TestResult } = {}
    local root = Registry.getRoot()
    local start = os.clock()

    -- Top-level tests (registered outside any describe)
    for _, test in ipairs(root.tests) do
        local r = runTest(Deps, root, test, config, "", callbacks)
        table.insert(results, r)
    end

    -- Suites
    for _, suite in ipairs(root.children) do
        runSuite(Deps, suite, results, config, "", callbacks)
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