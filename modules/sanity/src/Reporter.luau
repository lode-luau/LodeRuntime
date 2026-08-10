--!strict

--[[
    Reporter for the Sanity framework.

    Streams test progress live to the terminal when stdout is a TTY: the
    currently running test is redrawn on a single line (carriage return + ANSI
    erase) with a spinner and the elapsed milliseconds ticking up, and the line
    is finalized with a ✓/✗ when the test ends. When stdout is not a TTY, each
    result line is still printed as soon as its test completes. When live mode
    is disabled entirely, report() falls back to the block report.
]]

local Deps = require("./Deps")

local ANSI = {
    reset = "\27[0m",
    red = "\27[31m",
    green = "\27[32m",
    yellow = "\27[33m",
    dim = "\27[2m",
    bold = "\27[1m",
    eraseLine = "\27[2K",
}

local SPINNER = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }

type State = {
    isTTY: boolean,
    anyLive: boolean,
    active: boolean,
    suite: string,
    name: string,
    frame: number,
}

local state: State = {
    isTTY = false,
    anyLive = false,
    active = false,
    suite = "",
    name = "",
    frame = 0,
}

local stdio = Deps.stdio

local function color(code: string, text: string): string
    if state.isTTY then
        return code .. text .. ANSI.reset
    end
    return text
end

--- Detects TTY support and prints the runner header.
local function begin()
    local ok, tty = pcall(function()
        return stdio.stdout:isTTY()
    end)
    if ok then
        state.isTTY = tty
    end

    state.anyLive = false
    state.active = false
    state.frame = 0

    stdio.print("")
    stdio.print(color(ANSI.bold, "  Sanity Test Runner"))
    stdio.print("")
end

--- Erases the currently redrawn live line (TTY only).
local function clearLine()
    if state.isTTY then
        stdio.write("\r" .. ANSI.eraseLine)
    end
end

--- Redraws the live "running" line for the current test with elapsed ms.
local function drawRunning(ms: number)
    local frame = (state.frame % #SPINNER) + 1
    local spinnerChar: string = SPINNER[frame] or ""
    local line = "  "
        .. color(ANSI.yellow, spinnerChar)
        .. " "
        .. state.suite
        .. state.name
        .. color(ANSI.dim, string.format(" (%dms)", math.floor(ms)))
    stdio.write("\r" .. ANSI.eraseLine .. line)
end

--- Prints a single result line plus failure details (shared by live + block modes).
local function printResultLine(result: any)
    local suite = result.suite
    local name = result.name

    if result.status == "passed" then
        stdio.print("  " .. color(ANSI.green, "✓ ") .. suite .. name
            .. color(ANSI.dim, string.format(" (%.0fms)", result.durationMs)))
    elseif result.status == "skipped" then
        -- The error is prefixed with the source location and the [SKIPPED]
        -- marker (e.g. "init.luau:214: [SKIPPED] reason"); strip both.
        local msg = tostring(result.error)
        msg = msg:gsub("^.-:%d+: %[SKIPPED%] ?", "")
        stdio.print("  " .. color(ANSI.yellow, "○ ") .. suite .. name
            .. color(ANSI.dim, string.format(" [skipped: %s]", msg)))
    else
        stdio.print("  " .. color(ANSI.red, "✗ ") .. suite .. name
            .. color(ANSI.dim, string.format(" (%.0fms)", result.durationMs)))

        local err = result.error
        if typeof(err) == "table" and err.kind == "AssertionError" then
            stdio.print(color(ANSI.red, "      " .. tostring(err.message)))
            stdio.print(color(ANSI.dim, "      expected: " .. tostring(err.expected)))
            stdio.print(color(ANSI.dim, "      received: " .. tostring(err.received)))
        else
            local errStr = tostring(result.error)
            local formatted = "      " .. errStr:gsub("\n", "\n      ")
            stdio.print(color(ANSI.red, formatted))
        end
        stdio.print("")
    end
end

--- Called when a test starts running.
local function testStarted(suite: string, name: string)
    state.suite = suite
    state.name = name
    state.active = true
    state.frame = 0
    if state.isTTY then
        drawRunning(0)
    end
end

--- Called periodically while a test runs to refresh the elapsed time.
local function testProgress(suite: string, name: string, ms: number)
    if state.active and state.isTTY then
        state.frame += 1
        drawRunning(ms)
    end
end

--- Called when a test finishes; finalizes its line with the result.
local function testEnded(result: any)
    state.active = false
    state.anyLive = true

    clearLine()
    printResultLine(result)
end

--- Prints the summary (and the full block report when live mode was not used).
--- `totalDurationMs` is the wall-clock run time; `testsElapsedMs` is the sum of
--- the individual test durations. The difference is the runner overhead.
local function report(results: { any }, totalDurationMs: number, testsElapsedMs: number, config: any)
    if not state.anyLive then
        -- Non-live mode: print the whole block report (header + lines + summary).
        local ok, tty = pcall(function()
            return stdio.stdout:isTTY()
        end)
        if ok then
            state.isTTY = tty
        end

        stdio.print("")
        stdio.print(color(ANSI.bold, "  Sanity Test Runner"))
        stdio.print("")

        for _, r in ipairs(results) do
            printResultLine(r)
        end
        stdio.print("")
    else
        -- Live mode: clear any leftover live line and print only the summary.
        clearLine()
        stdio.print("")
    end

    local passed = 0
    local failed = 0
    local skipped = 0

    for _, r in ipairs(results) do
        if r.status == "passed" then
            passed += 1
        elseif r.status == "skipped" then
            skipped += 1
        else
            failed += 1
        end
    end

    local runnerElapsedMs = totalDurationMs - testsElapsedMs

    local summary = color(ANSI.green, tostring(passed) .. " passed")
        .. "  "
        .. color(ANSI.red, tostring(failed) .. " failed")
    if skipped > 0 then
        summary = summary .. "  " .. color(ANSI.yellow, tostring(skipped) .. " skipped")
    end

    stdio.print(string.format(
        "  %s  %s",
        summary,
        color(ANSI.dim, string.format(
            "(%.0fms total · %.0fms tests · %.0fms runner)",
            totalDurationMs,
            testsElapsedMs,
            runnerElapsedMs
        ))
    ))
    stdio.print("")
end

return {
    begin = begin,
    testStarted = testStarted,
    testProgress = testProgress,
    testEnded = testEnded,
    report = report,
}
