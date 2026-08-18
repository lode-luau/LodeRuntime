
--- A function or coroutine thread that can be executed.
type Executable = ((...any) -> ...any) | thread

--[[
    Native Task Module: Type definitions for Luau LSP & Autocomplete.
    Exports the Lode::Task C++ scheduler functionality to Luau.
    
    Usage:
    ```lua
    local task = require("@task")
    
    task.spawn(function()
        print("Running in a new thread")
        task.wait(1.5)
        print("1.5 seconds later")
    end)
    
    task.delay(5, print, "5 seconds passed")
    ```
]]
export type TaskModule = {
    --- Yields the current thread for a specified number of seconds.
    --- @return The actual time yielded.
    wait: (
        --- Optional number of seconds to wait. If nil, waits for the next tick.
        seconds: number?
    ) -> number,
    
    --- Spawns a function or coroutine immediately.
    --- @return The running thread.
    spawn: (
        --- The function or thread to execute.
        fnOrCo: Executable,
        --- Additional arguments to pass.
        ...any
    ) -> thread,
    
    --- Defers execution of a function or coroutine until the next engine tick.
    --- @return The running thread.
    defer: (
        --- The function or thread to execute.
        fnOrCo: Executable,
        --- Additional arguments to pass.
        ...any
    ) -> thread,
    
    --- Delays execution of a function or coroutine by a specified number of seconds.
    --- @return The running thread.
    delay: (
        --- The delay in seconds.
        delaySeconds: number,
        --- The function or thread to execute.
        fnOrCo: Executable,
        --- Additional arguments to pass.
        ...any
    ) -> thread,
    
    --- Cancels a scheduled task or running thread.
    cancel: (
        --- The thread or timer ID to cancel.
        target: Executable | number
    ) -> (),
    
    --- Sets a callback to execute after a specified delay in milliseconds.
    --- @return The timer ID.
    setTimeout: (
        --- The function to execute.
        callback: (...any) -> ...any,
        --- The delay in milliseconds.
        delayMs: number,
        --- Additional arguments to pass.
        ...any
    ) -> number,
    
    --- Clears a scheduled timeout.
    clearTimeout: (
        --- The timer ID to clear.
        timerId: number
    ) -> (),
    
    --- Sets a callback to execute repeatedly at a specified interval in milliseconds.
    --- @return The timer ID.
    setInterval: (
        --- The function to execute.
        callback: (...any) -> ...any,
        --- The interval in milliseconds.
        intervalMs: number,
        --- Additional arguments to pass.
        ...any
    ) -> number,
    
    --- Clears a scheduled interval.
    clearInterval: (
        --- The timer ID to clear.
        timerId: number
    ) -> (),
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string,
}

return {} :: TaskModule, {} :: ModuleMetaInfo
