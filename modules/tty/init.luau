--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents a connection to a signal.
export type SignalConnection = {
    Connected: boolean,
    Disconnect: (self: SignalConnection) -> (),
}

--- A native signal implementation for events.
export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

--- Supported TTY modes for the terminal.
export type TtyMode = "normal" | "raw" | "io"

--- Represents an interactive terminal stream (stdin/stdout/stderr).
export type TtyStream = {
    --- Writes string or buffer data to the terminal.
    Write: (self: TtyStream, data: string | buffer) -> (),

    --- Writes a string to the terminal followed by a newline.
    WriteLine: (self: TtyStream, data: string) -> (),

    --- Reads a specified number of bytes from the terminal. 
    --- Yields the current coroutine until the data is available.
    Read: (self: TtyStream, bytes: number?) -> string,

    --- Reads a specified number of bytes into a buffer.
    --- Yields the current coroutine until the data is available.
    ReadBuffer: (self: TtyStream, bytes: number?) -> buffer,

    --- Reads a line of text from the terminal.
    --- Yields the current coroutine until a newline is encountered.
    ReadLine: (self: TtyStream) -> string?,
    
    --- Starts reading data asynchronously, firing DataReceived signals.
    StartStreaming: (self: TtyStream) -> (),

    --- Stops the asynchronous data stream.
    StopStreaming: (self: TtyStream) -> (),

    --- Signal fired when data is received in streaming mode.
    DataReceived: Signal<buffer>,

    --- Signal fired when the stream ends (e.g., Ctrl+D or stream closed).
    EndOfStream: Signal<>,

    --- Signal fired on unrecoverable reading errors.
    ErrorOccurred: Signal<string>,

    --- Returns the terminal window's width and height in characters.
    --- @return A table with width and height properties.
    GetWindowSize: (self: TtyStream) -> { width: number, height: number },

    --- Changes the terminal mode.
    --- "normal" = standard line buffered input.
    --- "raw" = captures keystrokes directly.
    --- "io" = ignores all escape sequences.
    SetMode: (self: TtyStream, mode: TtyMode) -> (),

    --- Checks if the stream is currently open.
    --- @return True if open, false otherwise
    IsOpen: (self: TtyStream) -> boolean,

    --- Closes the terminal stream gracefully.
    Close: (self: TtyStream) -> (),

    --- Destroys the terminal stream immediately.
    Destroy: (self: TtyStream) -> (),
}

--- The `tty` module provides the native `TtyStream` implementation wrapping `uv_tty_t`.
--- It allows raw terminal manipulation and reactive reading.
---
--- Usage:
--- ```lua
--- local tty = require("@tty")
--- if tty.IsTTY(1) then
---     local out = tty.TtyStream:Create(1, false)
---     local size = out:GetWindowSize()
---     out:WriteLine("Hello! Screen is " .. size.width .. "x" .. size.height)
--- end
--- ```
export type TtyModule = {
    --- The TtyStream namespace.
    --- Provides streams wrapping a terminal file descriptor.
    TtyStream: {
        --- Creates a new TtyStream from a file descriptor.
        --- @param fd The file descriptor (0 for stdin, 1 for stdout, 2 for stderr)
        --- @param readable True if the stream should be readable (like stdin)
        --- @return A new TtyStream instance
        Create: (self: any, fd: number, readable: boolean) -> TtyStream
    },

    --- Checks if a given file descriptor is a TTY.
    IsTTY: (fd: number) -> boolean,
}

return {} :: TtyModule
