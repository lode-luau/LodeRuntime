--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents the operating system platform.
export type PlatformType = "Windows" | "Linux" | "Darwin" | "Haiku" | "FreeBSD" | "OpenBSD" | "NetBSD" | "SunOS" | "QNX" | "OS/400" | "AIX" | string

--- Represents the CPU architecture.
export type ArchitectureType = "x86_64" | "AMD64" | "x86" | "i386" | "i686" | "arm" | "arm64" | "aarch64" | "mips" | "mips64" | "ppc" | "ppc64" | "s390x" | "riscv64" | "wasm32" | "wasm64" | string

--- Represents the underlying OS handle type detected by libuv.
export type HandleType = "tty" | "pipe" | "file" | "tcp" | "udp" | "unknown"

--- Represents a connection to a signal.
export type SignalConnection = {
    Connected: boolean,
    Disconnect: (self: SignalConnection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

--- Represents a pipe stream connected to a child process's stdio.
export type ProcessPipeStream = {
    DataReceived: Signal<buffer>,
    EndOfStream: Signal<()>,
    ErrorOccurred: Signal<string>,

    Write: (self: ProcessPipeStream, data: string | buffer) -> (),
    WriteLine: (self: ProcessPipeStream, data: string) -> (),
    Read: (self: ProcessPipeStream, bytes: number?) -> string,
    ReadBuffer: (self: ProcessPipeStream, bytes: number?) -> buffer,
    ReadLine: (self: ProcessPipeStream) -> string?,
    StartStreaming: (self: ProcessPipeStream) -> (),
    StopStreaming: (self: ProcessPipeStream) -> (),
    Close: (self: ProcessPipeStream) -> (),
}

--- Options for spawning a child process.
export type SpawnOptions = {
    args: { string }?,
    cwd: string?,
    env: { [string]: string }?,
    stdio: ({ string } | string)?,
    detached: boolean?,
    hide: boolean?,
}

--- Represents a spawned child process.
export type Process = {
    pid: number,
    stdin: ProcessPipeStream?,
    stdout: ProcessPipeStream?,
    stderr: ProcessPipeStream?,

    Exited: Signal<number, number>,
    ErrorOccurred: Signal<string>,

    Kill: (self: Process, signal: number?) -> (),
    Wait: (self: Process) -> (number, number),
    GetPid: (self: Process) -> number,
    GetExitCode: (self: Process) -> number?,
    GetTermSignal: (self: Process) -> number?,
    IsRunning: (self: Process) -> boolean,
    Close: (self: Process) -> (),
    Destroy: (self: Process) -> (),
}

--- System Module (sys).
--- Provides synchronous access to operating system information,
--- subprocess creation, and process controls.
--- 
--- Usage:
--- ```lua
--- local sys = require("@sys")
--- 
--- local child = sys.spawn("cmd.exe", { args = { "/c", "echo hello" } })
--- local line = child.stdout:ReadLine()
--- print("Child output:", line)
--- local exitCode = child:Wait()
--- print("Exit code:", exitCode)
--- ```
export type SysModule = {
    --- Returns the platform string representing the current OS.
    --- Examples: "Windows", "Linux", "Darwin".
    --- @return The OS platform name
    GetPlatform: () -> PlatformType,

    --- Returns the architecture of the current machine (e.g., "x86_64", "arm64").
    --- @return The CPU architecture
    GetArchitecture: () -> ArchitectureType,

    --- Returns the operating system release string.
    --- @return The OS release string
    GetRelease: () -> string,

    --- Returns the operating system version string.
    --- @return The OS version string
    GetVersion: () -> string,

    --- Returns the hostname of the current machine.
    --- @return The hostname
    GetHostname: () -> string,

    --- Returns the total amount of RAM available on the machine in bytes.
    --- @return Total memory in bytes
    GetTotalMemory: () -> number,

    --- Returns the amount of free RAM available on the machine in bytes.
    --- @return Free memory in bytes
    GetFreeMemory: () -> number,

    --- Returns the system uptime in seconds.
    --- @return Uptime in seconds
    GetUptime: () -> number,

    --- Returns the number of threads available for parallel work.
    GetAvailableParallelism: () -> number,

    --- Returns the number of logical CPUs.
    GetCpuCount: () -> number,

    --- Returns model and speed information for each logical CPU.
    GetCpuInfo: () -> { { model: string, speed: number } },

    --- Returns the operating system temporary directory.
    GetTmpDir: () -> string,

    --- Returns the current user's home directory.
    GetHomeDir: () -> string,

    --- Returns one-, five-, and fifteen-minute load averages.
    GetLoadAverage: () -> { number },

    --- Returns a read-only signal for an operating system signal.
    --- Supported names include SIGINT, SIGTERM, SIGHUP, and SIGWINCH where available.
    Signal: (name: string) -> Signal<number>,

    --- Retrieves the value of an environment variable.
    --- @param key The name of the environment variable
    --- @return The value of the environment variable, or nil if not found
    GetEnv: (key: string) -> string?,

    --- Sets or overrides an environment variable.
    --- @param key The name of the environment variable
    --- @param value The value to set
    SetEnv: (key: string, value: string) -> (),
    
    --- Gets the current working directory of the process.
    --- @return The current working directory path
    GetCwd: () -> string,

    --- Changes the current working directory of the process.
    --- Throws an error if the path is invalid or lacks permissions.
    --- @param dir The directory path to change to
    Chdir: (dir: string) -> (),

    --- Exits the process immediately with the specified exit code.
    --- @param code The exit code (default is usually 0)
    exit: (code: number) -> (),

    --- Attempts to guess the underlying libuv handle type for a given file descriptor.
    --- @param fd The file descriptor (e.g., 0 for stdin)
    --- @return The type of the handle
    GuessHandleType: (fd: number) -> HandleType,

    --- Spawns a child process asynchronously.
    --- @param command Executable name or path, or table with options.
    --- @param options Optional configuration table.
    --- @return The spawned Process object.
    spawn: (command: string, options: SpawnOptions?) -> Process,
    Spawn: (command: string, options: SpawnOptions?) -> Process,
}

return {} :: SysModule
