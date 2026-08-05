--!strict
--!native

--[[
    Sanity CLI runner.

    Entry point for running the test suite:
        lode_runtime modules/sanity/run.luau

    Steps:
      1. Load optional config (sanity.config.luau).
      2. Discover test files (*.test.luau) under the configured testDir.
      3. Require each test file (registers tests in the shared registry).
      4. Run the suite and print a report.
      5. Exit with a non-zero code if any test failed (via error()).
]]

local sanity = require("./init")
local Deps = require("./src/Deps")
local Discovery = require("./src/Discovery")

local config = {
    testDir = "tests",
    pattern = "%.test%.luau$",
    timeout = 5,
    exitOnFailure = true,
    live = true,
}

local ok, loaded = pcall(require, "./sanity.config")
if ok and typeof(loaded) == "table" then
    for k, v in pairs(loaded) do
        config[k] = v
    end
end

local fs = Deps.fs
local path = fs.path

local selfDir = path.resolve("@self")
local testDir = path.join(selfDir, config.testDir)

local files = Discovery.discover(testDir, config.pattern)

if #files == 0 then
    Deps.stdio.print("No test files found in " .. testDir)
    return
end

-- Require each discovered test file. Using @self/<testDir>/<rel> guarantees
-- resolution relative to the sanity package directory. The path is dynamic,
-- so we call `require` through a plain function reference to bypass Luau's
-- static require-path checking.
local req: (string) -> any = require
for _, rel in ipairs(files) do
    local reqPath = "@self/" .. config.testDir .. "/" .. rel
    req(reqPath)
end

-- Live streaming mode (enabled by default when stdout is a TTY): each test's
-- progress is redrawn on a single line with elapsed ms, finalized on completion.
local callbacks = nil
if config.live ~= false then
    local Reporter = require("./src/Reporter")
    Reporter.begin()
    callbacks = {
        onTestStart = Reporter.testStarted,
        onTestProgress = Reporter.testProgress,
        onTestEnd = Reporter.testEnded,
    }
end

local results, totalDuration, testsElapsed = sanity.run(config, callbacks)
sanity.report(results, totalDuration, testsElapsed, config)

local failed = 0
for _, r in ipairs(results) do
    if r.status == "failed" then
        failed += 1
    end
end

if failed > 0 and config.exitOnFailure then
    error(string.format("%d test(s) failed", failed))
end