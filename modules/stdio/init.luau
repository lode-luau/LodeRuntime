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

--- Represents a standard I/O stream object (stdin/stdout/stderr).
export type StdioStream = {
    -- Signals
    DataReceived: Signal<buffer>,
    EndOfStream: Signal<()>,
    ErrorOccurred: Signal<string>,
    WindowResized: Signal<number, number>,

    -- Methods (PascalCase)
    Write: (self: StdioStream, data: string | buffer) -> (),
    WriteLine: (self: StdioStream, data: string) -> (),
    Read: (self: StdioStream, bytes: number?) -> string,
    ReadBuffer: (self: StdioStream, bytes: number?) -> buffer,
    ReadLine: (self: StdioStream) -> string?,
    ReadInto: (self: StdioStream, buf: buffer, offset: number?, length: number?) -> number,
    ReadAsync: (self: StdioStream, callback: (data: string) -> (), bytes: number?) -> (),
    ReadBufferAsync: (self: StdioStream, callback: (data: buffer) -> (), bytes: number?) -> (),
    ReadIntoAsync: (self: StdioStream, buf: buffer, callback: (bytesRead: number) -> (), offset: number?, length: number?) -> (),
    StartStreaming: (self: StdioStream) -> (),
    StopStreaming: (self: StdioStream) -> (),
    GetWindowSize: (self: StdioStream) -> { width: number, height: number, columns: number, rows: number },
    SetMode: (self: StdioStream, mode: string | number) -> (),
    SetRawMode: (self: StdioStream, enable: boolean) -> (),
    IsTTY: (self: StdioStream) -> boolean,
    IsOpen: (self: StdioStream) -> boolean,
    Close: (self: StdioStream) -> (),
    Destroy: (self: StdioStream) -> (),

    -- Backward Compatibility Aliases (camelCase)
    write: (self: StdioStream, data: string | buffer) -> (),
    writeLine: (self: StdioStream, data: string) -> (),
    read: (self: StdioStream, bytes: number?) -> string,
    readBuffer: (self: StdioStream, bytes: number?) -> buffer,
    readLine: (self: StdioStream) -> string?,
    readInto: (self: StdioStream, buf: buffer, offset: number?, length: number?) -> number,
    readAsync: (self: StdioStream, callback: (data: string) -> (), bytes: number?) -> (),
    readBufferAsync: (self: StdioStream, callback: (data: buffer) -> (), bytes: number?) -> (),
    readIntoAsync: (self: StdioStream, buf: buffer, callback: (bytesRead: number) -> (), offset: number?, length: number?) -> (),
    isTTY: (self: StdioStream) -> boolean,
    setRawMode: (self: StdioStream, enable: boolean) -> (),
    getWindowSize: (self: StdioStream) -> (number, number),
}

--- Standard Input/Output Module (stdio).
--- Provides streams for interacting with the console and OS standard file descriptors.
---
--- Usage:
--- ```lua
--- local stdio = require("@stdio")
---
--- stdio.stdout:WriteLine("Hello stdout!")
--- stdio.print("Hello world!")
--- ```
export type StdioModule = {
    --- Standard Input Stream (File descriptor 0)
    stdin: StdioStream,

    --- Standard Output Stream (File descriptor 1)
    stdout: StdioStream,

    --- Standard Error Stream (File descriptor 2)
    stderr: StdioStream,

    --- Stream class factory
    StdioStream: {
        Create: (self: any, fd: number, readable: boolean) -> StdioStream,
    },

    --- Prints space-separated values to stdout, followed by a newline.
    print: (...any) -> (),

    --- Prints space-separated values to stderr, followed by a newline.
    eprint: (...any) -> (),

    --- Writes directly to stdout without a newline.
    write: (data: string | buffer) -> (),

    --- Displays an optional prompt message and yields until the user inputs a line of text.
    prompt: (message: string?) -> string,

    --- Clears the console screen.
    clear: () -> (),

    --- Displays an interactive selection menu using the arrow keys.
    select: (prompt: string, options: {string}) -> (number, string),

    --- Checks if a given file descriptor is connected to a TTY.
    IsTTY: (fd: number) -> boolean,
    isTTY: (fd: number) -> boolean,
}

return {} :: StdioModule
