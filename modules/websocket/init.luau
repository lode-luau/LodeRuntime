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

--- The lifecycle state of a WebSocket connection.
export type WebSocketState = "connecting" | "open" | "closing" | "closed"

--- Represents a network address for WebSockets.
export type WebSocketAddress = {
    --- The host IP address or hostname
    host: string,
    --- The port number
    port: number,
}

--- Options for connecting a WebSocket client.
export type ConnectOptions = {
    --- Additional HTTP headers to send during the handshake
    headers: { [string]: string }?,
    --- The connection timeout in milliseconds
    timeout: number?,
}

--- Base options for a WebSocket instance.
export type WebSocketOptions = {
    -- Reserved for future client configuration.
}

--- Options for creating a WebSocket server.
export type ServerOptions = {
    --- The maximum length to which the queue of pending connections for the socket may grow.
    backlog: number?,
}

--- Represents a WebSocket connection.
export type WebSocket = {
    -- Events
    
    --- Fired when the WebSocket successfully completes the handshake.
    Connected: Signal<()>,
    --- Fired when a message is received from the remote endpoint.
    MessageReceived: Signal<string>,
    --- Fired when the WebSocket is disconnected, providing the close code and reason if any.
    Disconnected: Signal<number?, string?>,
    --- Fired when an error occurs.
    ErrorOccurred: Signal<string>,

    -- Methods
    
    --- Connects the WebSocket to a remote URL.
    Connect: (
        self: WebSocket,
        --- The WebSocket URL (e.g. "ws://example.com")
        url: string,
        --- Optional connection options
        opts: ConnectOptions?
    ) -> (),
    
    --- Sends data over the connected WebSocket.
    Send: (
        self: WebSocket,
        --- The string or buffer to send
        data: string | buffer
    ) -> (),
    
    --- Sends a Ping frame over the WebSocket.
    Ping: (
        self: WebSocket,
        --- Optional payload for the ping frame
        data: string | buffer?
    ) -> (),
    
    --- Closes the WebSocket gracefully.
    Close: (
        self: WebSocket,
        --- Optional close status code
        code: number?,
        --- Optional close reason string
        reason: string?
    ) -> (),
    
    --- Destroys the WebSocket immediately.
    Destroy: (self: WebSocket) -> (),

    -- State (read-only)
    
    --- Checks if the WebSocket is currently open.
    --- @return True if connected, false otherwise
    IsConnected: (self: WebSocket) -> boolean,
    
    --- Gets the current ReadyState of the WebSocket.
    --- @return The WebSocketState enum value
    ReadyState: (self: WebSocket) -> WebSocketState,
}

--- Represents a WebSocket server that listens for incoming connections.
export type WebSocketServer = {
    -- Events
    
    --- Fired when a new WebSocket client completes the handshake with the server.
    ClientConnected: Signal<WebSocket>,
    --- Fired when a server error occurs.
    ErrorOccurred: Signal<string>,

    -- Methods
    
    --- Starts listening on the specified port and optional host.
    Listen: (
        self: WebSocketServer,
        --- The port to listen on
        port: number,
        --- Optional hostname or IP to bind to
        host: string?
    ) -> (),
    
    --- Closes the server, stopping it from accepting new connections.
    Close: (self: WebSocketServer) -> (),
    
    --- Destroys the server immediately.
    Destroy: (self: WebSocketServer) -> (),

    -- State (read-only)
    
    --- Checks if the server is currently listening.
    --- @return True if listening, false otherwise
    IsListening: (self: WebSocketServer) -> boolean,
    
    --- Gets the local address the server is bound to.
    --- @return The local WebSocketAddress
    LocalAddress: (self: WebSocketServer) -> WebSocketAddress,
}

--[[
    Native WebSocket (RFC 6455) module. 
    All network I/O is asynchronous and runs on the LibUV event loop.
    Only the `ws://` scheme is currently supported.
    
    Usage:
    ```lua
    local websocket = require("@websocket")
    
    local ws = websocket.WebSocket:Create()
    ws.Connected:Connect(function()
        ws:Send("Hello from Luau!")
    end)
    ws.MessageReceived:Connect(function(msg)
        print("WebSocket Message:", msg)
    end)
    ws:Connect("ws://echo.websocket.org")
    ```
]]
export type WebSocketModule = {
    --[[
        The WebSocket Client namespace.
        Provides functionality to connect to remote WebSocket servers.
        
        Usage:
        ```lua
        local ws = websocket.WebSocket:Create()
        ws:Connect("ws://echo.websocket.org")
        ```
    ]]
    WebSocket: {
        --- Creates a new WebSocket instance.
        --- @return A new WebSocket instance
        Create: (
            self: any,
            --- Optional configuration
            opts: WebSocketOptions?
        ) -> WebSocket
    },
    --[[
        The WebSocket Server namespace.
        Provides functionality to listen for incoming WebSocket connections.
        
        Usage:
        ```lua
        local server = websocket.WebSocketServer:Create()
        server:Listen(8080)
        ```
    ]]
    WebSocketServer: {
        --- Creates a new WebSocket server instance.
        --- @return A new WebSocketServer instance
        Create: (
            self: any,
            --- Optional server configuration
            opts: ServerOptions?
        ) -> WebSocketServer
    },
}

return {} :: WebSocketModule
