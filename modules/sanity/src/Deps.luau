--!strict
--!native

--[[
    Centralized native-module dependencies for the Sanity framework.

    Sanity is a pure-Luau module that relies on three native modules:
      * stdio      -> output / reporting
      * filesystem -> test discovery + fixtures
      * task       -> async execution (spawn, wait, cancel)

    NOTE: the `task` module currently lives in `temp/task_module` (not under
    `modules/`). If it is moved to `modules/`, update the path below.
]]

local stdio = require("../../stdio")
local fs = require("../../filesystem")
local task = require("../../../temp/task_module")

return {
    stdio = stdio,
    fs = fs,
    task = task,
}