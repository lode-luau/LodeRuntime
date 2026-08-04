--!strict
--!native

--[[
    Sanity configuration.

    This file is loaded by run.luau (via require) and merged over the default
    config. `testDir` is relative to the sanity package directory (@self).
]]

return {
    -- Directory (relative to @self) to scan for test files.
    testDir = "tests",

    -- Lua pattern matched against file names to identify test files.
    pattern = "%.test%.luau$",

    -- Per-test timeout in seconds. Tests that exceed this are marked failed.
    timeout = 5,

    -- When true, the runner throws at the end if any test failed, causing the
    -- runtime to exit with a non-zero code.
    exitOnFailure = true,
}