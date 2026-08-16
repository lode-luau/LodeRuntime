--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents the operating system platform.
export type PlatformType = "Windows" | "Linux" | "Darwin" | "Haiku" | "FreeBSD" | "OpenBSD" | "NetBSD" | "SunOS" | "QNX" | "OS/400" | "AIX" | string

--- Represents the CPU architecture.
export type ArchitectureType = "x86_64" | "AMD64" | "x86" | "i386" | "i686" | "arm" | "arm64" | "aarch64" | "mips" | "mips64" | "ppc" | "ppc64" | "s390x" | "riscv64" | "wasm32" | "wasm64" | string

--- Represents the underlying OS handle type detected by libuv.
export type HandleType = "tty" | "pipe" | "file" | "tcp" | "udp" | "unknown"

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> any,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> any,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

--- System Module (sys).
--- Provides synchronous access to operating system information and 
--- low-level process controls.
--- 
--- Usage:
--- ```lua
--- local sys = require("@sys")
--- 
--- print("Platform:", sys.GetPlatform())
--- print("Free RAM:", sys.GetFreeMemory())
--- 
--- sys.SetEnv("MY_VAR", "123")
--- print(sys.GetEnv("MY_VAR"))
--- 
--- local handleType = sys.GuessHandleType(1)
--- print("Stdout is:", handleType)
--- 
--- sys.exit(0)
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
    --- Crucial for initializing standard IO streams dynamically.
    --- @param fd The file descriptor (e.g., 0 for stdin)
    --- @return The type of the handle
    GuessHandleType: (fd: number) -> HandleType, 
}

return {} :: SysModule
