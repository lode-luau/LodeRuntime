--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents a connection to a signal.
export type SignalConnection = {
    --- Whether this connection is still active.
    Connected: boolean,
    --- Disconnects this connection, preventing the callback from being fired.
    --- @param self The connection instance
    Disconnect: (self: SignalConnection) -> (),
}

--- A native signal implementation for events.
export type Signal<T...> = {
    --- Connects a callback to the signal.
    --- @return A connection object that can be disconnected
    Connect: (
        self: Signal<T...>,
        --- The function to call when the signal fires
        callback: (T...) -> ()
    ) -> SignalConnection,

    --- Creates a connection that will only fire once.
    --- @return A connection object that can be disconnected
    Once: (
        self: Signal<T...>,
        --- The function to call when the signal fires
        callback: (T...) -> ()
    ) -> SignalConnection,

    --- Yields the current thread until the signal fires.
    --- @return The arguments passed to Fire
    Wait: (
        self: Signal<T...>,
        --- Optional max seconds to wait
        timeout: number?
    ) -> T...,
}

--- Represents an active named-pipe stream (or a redirected file descriptor).
export type PipeStream = {
    -- Core IO

    --- Connects to a named pipe server.
    --- Yields the current coroutine until the connection is established.
    Connect: (
        self: PipeStream,
        --- The named pipe path (e.g. "\\\\.\\pipe\\name" or a bare name)
        path: string
    ) -> (),

    --- Opens the stream from a file descriptor (used by stdio).
    OpenFD: (
        self: PipeStream,
        --- The file descriptor number
        fd: number
    ) -> (),

    --- Writes string or buffer data to the stream.
    Write: (
        self: PipeStream,
        --- The data to write
        data: string | buffer
    ) -> (),

    --- Writes a string to the stream followed by a newline.
    WriteLine: (
        self: PipeStream,
        --- The string to write
        data: string
    ) -> (),

    --- Reads a specified number of bytes from the stream.
    --- Yields the current coroutine until the data is available.
    Read: (
        self: PipeStream,
        --- Optional number of bytes to read (all available if omitted)
        bytes: number?
    ) -> string,

    --- Reads a specified number of bytes into a buffer.
    --- Yields the current coroutine until the data is available.
    ReadBuffer: (
        self: PipeStream,
        --- Optional number of bytes to read (all available if omitted)
        bytes: number?
    ) -> buffer,

    --- Reads a line of text from the stream.
    --- Yields the current coroutine until a newline is encountered.
    ReadLine: (self: PipeStream) -> string?,

    --- Starts reading data asynchronously, firing DataReceived signals.
    StartStreaming: (self: PipeStream) -> (),

    --- Stops the asynchronous data stream.
    StopStreaming: (self: PipeStream) -> (),

    -- Signals

    --- Signal fired when data is received in streaming mode.
    DataReceived: Signal<buffer>,

    --- Signal fired when the stream ends (EOF or closed).
    EndOfStream: Signal<>,

    --- Signal fired on unrecoverable errors.
    ErrorOccurred: Signal<string>,

    -- State

    --- Checks if the stream is currently open.
    --- @return True if open, false otherwise
    IsOpen: (self: PipeStream) -> boolean,

    --- Closes the stream gracefully.
    Close: (self: PipeStream) -> (),

    --- Destroys the stream immediately.
    Destroy: (self: PipeStream) -> (),
}

--- Represents a named-pipe server for local IPC.
export type PipeServer = {
    -- Signals

    --- Fired when a new client connects to the server.
    ConnectionReceived: Signal<PipeStream>,
    --- Fired when a server error occurs.
    ErrorOccurred: Signal<string>,

    -- Methods

    --- Starts listening on the specified named pipe path.
    Listen: (
        self: PipeServer,
        --- The named pipe path (e.g. "\\\\.\\pipe\\name" or a bare name)
        path: string
    ) -> (),

    --- Yields the current coroutine until a client connects, then returns it.
    --- @return The accepted PipeStream
    Accept: (self: PipeServer) -> PipeStream,

    -- State

    --- Checks if the server is currently listening.
    --- @return True if listening, false otherwise
    IsListening: (self: PipeServer) -> boolean,

    --- Closes the server, stopping it from accepting new connections.
    Close: (self: PipeServer) -> (),

    --- Destroys the server immediately.
    Destroy: (self: PipeServer) -> (),
}

--[[
    Native Pipe module.
    Provides named-pipe (IPC) and redirected-file-descriptor streams using the
    LibUV event loop.

    Usage:
    ```lua
    local pipe = require("@pipe")

    -- Server (signal-based)
    local server = pipe.PipeServer:Create()
    server.ConnectionReceived:Connect(function(stream)
        stream:WriteLine("hello from server")
    end)
    server:Listen("myapp")

    -- Client
    local client = pipe.PipeStream:Create()
    client:Connect("myapp")
    client.ReadLine:Connect(function(line) print("got", line) end)
    ```
]]
export type PipeModule = {
    --[[
        The PipeStream namespace.
        Provides streams for connecting to named pipes or opening file descriptors.
    ]]
    PipeStream: {
        --- Creates a new PipeStream instance.
        --- @return A new PipeStream instance
        Create: (self: any) -> PipeStream
    },
    --[[
        The PipeServer namespace.
        Provides functionality to listen for incoming named-pipe connections.
    ]]
    PipeServer: {
        --- Creates a new PipeServer instance.
        --- @return A new PipeServer instance
        Create: (self: any) -> PipeServer
    },

    --- Checks if a given file descriptor is a named pipe.
    --- @return True if the fd is a named pipe, false otherwise
    IsPipe: (fd: number) -> boolean,
}

return {} :: PipeModule