--!strict
--!native

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

--- Represents a network address.
export type SocketAddress = {
    --- The host IP address or hostname
    host: string,
    --- The port number
    port: number,
}

--- Options for creating a TCP server.
export type ServerOptions = {
    --- The maximum length to which the queue of pending connections for the socket may grow.
    backlog: number?,
}

--- Represents a TCP socket for sending and receiving data.
export type TcpSocket = {
    -- Events
    
    --- Fired when the socket successfully connects.
    Connected: Signal<()>,
    --- Fired when data is received from the remote endpoint.
    MessageReceived: Signal<string>,
    --- Fired when the socket is disconnected.
    Disconnected: Signal<string?>,
    --- Fired when an error occurs on the socket.
    ErrorOccurred: Signal<string>,

    -- Methods
    
    --- Connects the socket to a remote host and port.
    Connect: (
        self: TcpSocket,
        --- The hostname or IP address to connect to
        host: string,
        --- The port to connect to
        port: number,
        --- Optional connection timeout in milliseconds
        timeout: number?
    ) -> (),
    
    --- Sends data over the connected socket.
    Send: (
        self: TcpSocket,
        --- The string or buffer to send
        data: string | buffer
    ) -> (),
    
    --- Closes the socket gracefully.
    Close: (self: TcpSocket) -> (),
    
    --- Destroys the socket immediately.
    Destroy: (self: TcpSocket) -> (),

    -- State (read-only)
    
    --- Checks if the socket is currently connected.
    --- @return True if connected, false otherwise
    IsConnected: (self: TcpSocket) -> boolean,
    
    --- Gets the local address of the socket.
    --- @return The local SocketAddress
    LocalAddress: (self: TcpSocket) -> SocketAddress,
    
    --- Gets the remote address the socket is connected to.
    --- @return The remote SocketAddress
    RemoteAddress: (self: TcpSocket) -> SocketAddress,
}

--- Represents a TCP server that listens for incoming connections.
export type Server = {
    -- Events
    
    --- Fired when a new client connects to the server.
    ClientConnected: Signal<TcpSocket>,
    --- Fired when a server error occurs.
    ErrorOccurred: Signal<string>,

    -- Methods
    
    --- Starts listening on the specified port and optional host.
    Listen: (
        self: Server,
        --- The port to listen on
        port: number,
        --- Optional hostname or IP to bind to
        host: string?
    ) -> (),
    
    --- Closes the server, stopping it from accepting new connections.
    Close: (self: Server) -> (),
    
    --- Destroys the server immediately.
    Destroy: (self: Server) -> (),

    -- State (read-only)
    
    --- Checks if the server is currently listening.
    --- @return True if listening, false otherwise
    IsListening: (self: Server) -> boolean,
    
    --- Gets the local address the server is bound to.
    --- @return The local SocketAddress
    LocalAddress: (self: Server) -> SocketAddress,
}

--[[
    Native TCP socket module.
    All network I/O is asynchronous and runs on the LibUV event loop.
    
    Usage:
    ```lua
    local socket = require("@socket")
    
    local server = socket.Server:Create()
    server.ClientConnected:Connect(function(client)
        client.MessageReceived:Connect(function(msg)
            client:Send("Echo: " .. msg)
        end)
    end)
    server:Listen(8080, "0.0.0.0")
    ```
]]
export type SocketModule = {
    --[[
        The TCP Server namespace.
        Provides functionality to listen for incoming TCP connections.
        
        Usage:
        ```lua
        local server = socket.Server:Create()
        server:Listen(8080)
        ```
    ]]
    Server: {
        --- Creates a new TCP server instance.
        --- @return A new Server instance
        Create: (
            self: any,
            --- Optional server configuration
            opts: ServerOptions?
        ) -> Server 
    },
    --[[
        The TCP Client Socket namespace.
        Provides functionality to connect to remote TCP endpoints.
        
        Usage:
        ```lua
        local client = socket.TcpSocket:Create()
        client:Connect("127.0.0.1", 8080)
        ```
    ]]
    TcpSocket: {
        --- Creates a new TCP socket instance.
        --- @return A new TcpSocket instance
        Create: (self: any) -> TcpSocket 
    },
    
    --- Resolves a hostname to an IP address synchronously.
    --- @return The resolved IP address
    resolve: (
        --- The hostname to resolve
        host: string
    ) -> string,
    
    --- Resolves a hostname to an IP address asynchronously.
    resolveAsync: (
        --- The hostname to resolve
        host: string,
        --- The callback invoked with the result or error
        callback: (address: string?, error: string?) -> ()
    ) -> (),
}

return {} :: SocketModule
