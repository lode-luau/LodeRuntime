-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--[[
    Path Module.
    Provides utilities for manipulating and resolving file and directory paths.
    
    Usage:
    ```lua
    local path = require("@path")
    
    local fullPath = path.join("myFolder", "subFolder", "file.txt")
    print(path.dirname(fullPath)) -- myFolder/subFolder
    print(path.basename(fullPath)) -- file.txt
    print(path.extname(fullPath)) -- .txt
    ```
]]
export type PathModule = {
    --- Resolves a sequence of paths or path segments into an absolute path.
    --- Supports `@self` relative to the package directory of the caller.
    --- @return The resolved absolute path.
    resolve: (
        --- The sequence of paths to resolve.
        ...string
    ) -> string,

    --- Joins a sequence of paths or path segments.
    --- @return The joined path string.
    join: (
        --- The sequence of paths to join.
        ...string
    ) -> string,

    --- Returns the directory name of a path.
    --- @return The directory name.
    dirname: (
        --- The path to evaluate.
        p: string
    ) -> string,

    --- Returns the last portion of a path.
    --- @return The base name.
    basename: (
        --- The path to evaluate.
        p: string
    ) -> string,

    --- Returns the extension of the path.
    --- @return The extension of the path.
    extname: (
        --- The path to evaluate.
        p: string
    ) -> string,
}

return {} :: PathModule
