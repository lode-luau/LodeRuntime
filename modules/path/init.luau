-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module path
--- Provides utilities for manipulating and resolving file and directory paths.

export type PathModule = {
    --- Resolves a sequence of paths or path segments into an absolute path.
    --- Supports `@self` relative to the package directory of the caller.
    --- @param ... The sequence of paths to resolve.
    --- @return The resolved absolute path.
    resolve: (...string) -> string,

    --- Joins a sequence of paths or path segments.
    --- @param ... The sequence of paths to join.
    --- @return The joined path string.
    join: (...string) -> string,

    --- Returns the directory name of a path.
    --- @param p The path to evaluate.
    --- @return The directory name.
    dirname: (p: string) -> string,

    --- Returns the last portion of a path.
    --- @param p The path to evaluate.
    --- @return The base name.
    basename: (p: string) -> string,

    --- Returns the extension of the path.
    --- @param p The path to evaluate.
    --- @return The extension of the path.
    extname: (p: string) -> string,
}

return {} :: PathModule
