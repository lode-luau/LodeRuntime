--!strict

--[[
    Centralized native-module dependencies for the Sanity framework.

    Sanity is a pure-Luau module that relies on three native modules:
      * stdio      -> output / reporting
      * filesystem -> test discovery + fixtures
      * task       -> async execution (spawn, wait, cancel)

    Sanity is runtime-agnostic: it runs on any Luau runtime (Lode, Roblox, Lute,
    Lune, ...) as long as that runtime provides modules with the same public API.
    The tryRequire calls below try a list of candidate module names in order —
    prepend your runtime's own module names (e.g. "@lune/fs") to load from the
    runtime you are using instead of the bundled Lode modules.

    NOTE: the `task` module lives in `modules/task` (bundled with the other
    Lode modules).
]]

local function tryRequire(...: string): any
    local candidates = { ... }
    for _, name in ipairs(candidates) do
        local ok, result = pcall(require, name)
        if ok then
            return result
        end
    end
    error(string.format("Failed to require any of: %s", table.concat(candidates, ", ")))
end

local stdio = tryRequire("@stdio", "../../stdio")
local fs = tryRequire("@filesystem", "../../filesystem")
local path = tryRequire("@filesystem/path", "../../filesystem/path")
local task = tryRequire("@task", "../../task")

return {
    stdio = stdio,
    fs = fs,
    path = path,
    task = task,
}