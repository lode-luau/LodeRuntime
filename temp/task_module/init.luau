-- Native Task Module: Type definitions for Luau LSP & Autocomplete
-- Exports the Lode::Task C++ scheduler functionality to Luau.

type Executable = ((...any) -> ...any) | thread

export type TaskModule = {
    wait: (seconds: number?) -> number,
    spawn: (fnOrCo: Executable, ...any) -> thread,
    defer: (fnOrCo: Executable, ...any) -> (),
    delay: (delaySeconds: number, fnOrCo: Executable, ...any) -> (),
    cancel: (target: Executable | number) -> (),
    setTimeout: (callback: (...any) -> ...any, delayMs: number, ...any) -> number,
    clearTimeout: (timerId: number) -> (),
    setInterval: (callback: (...any) -> ...any, intervalMs: number, ...any) -> number,
    clearInterval: (timerId: number) -> (),
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string,
}

return {} :: TaskModule, {} :: ModuleMetaInfo
