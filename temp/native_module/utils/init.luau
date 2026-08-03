-- Internal utility module for the native_module package.
-- Loaded via require("@self/utils") from inside the C++ native module.
-- This file sits at native_module/utils/init.luau, which is equivalent to native_module/utils.luau.

local utils = {}

function utils.formatGreeting(name: string): string
    return string.format("Hello, %s! (via @self/utils)", name)
end

function utils.double(n: number): number
    return n * 2
end

function utils.describe(): string
    return "I am utils/init.luau, loaded from inside the native_module package via @self"
end

return utils
