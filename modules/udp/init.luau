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

--- Represents a network address.
export type Address = {
    --- The host IP address or hostname
    host: string,
    --- The port number
    port: number
}

--- Represents a UDP socket for sending and receiving datagrams.
export type UdpSocket = {
    -- Events
    
    --- Fired when data is received from a remote endpoint.
    --- The callback receives the data payload and the remote Address.
    MessageReceived: Signal<string, Address>,
    --- Fired when an error occurs on the socket.
    ErrorOccurred: Signal<string>,

    -- Methods
    
    --- Binds the socket to a specific host and port to listen for incoming datagrams.
    Bind: (
        self: UdpSocket,
        --- The hostname or IP address to bind to
        host: string,
        --- The port to bind to
        port: number
    ) -> (),
    
    --- Sends a datagram to a specific remote host and port.
    Send: (
        self: UdpSocket,
        --- The string or buffer to send
        data: string | buffer,
        --- The destination hostname or IP address
        host: string,
        --- The destination port
        port: number
    ) -> (),
    
    --- Closes the socket gracefully.
    Close: (self: UdpSocket) -> (),
    
    --- Destroys the socket immediately.
    Destroy: (self: UdpSocket) -> (),
    
    -- State
    
    --- Gets the local address the socket is bound to.
    --- @return The local Address
    LocalAddress: (self: UdpSocket) -> Address,
}

--[[
    Native UDP module.
    Provides asynchronous UDP datagram networking using the LibUV event loop.
    
    Usage:
    ```lua
    local udp = require("@udp")
    
    local socket = udp.UdpSocket:Create()
    socket.MessageReceived:Connect(function(msg, addr)
        print("Received from " .. addr.host .. ":", msg)
    end)
    
    socket:Bind("0.0.0.0", 12345)
    socket:Send("Hello UDP!", "127.0.0.1", 12345)
    ```
]]
export type UdpModule = {
    --[[
        The UDP Socket namespace.
        Provides functionality to bind to ports and send/receive datagrams.
        
        Usage:
        ```lua
        local socket = udp.UdpSocket:Create()
        socket:Bind("0.0.0.0", 12345)
        ```
    ]]
    UdpSocket: {
        --- Creates a new UDP socket instance.
        --- @return A new UdpSocket instance
        Create: (self: any) -> UdpSocket
    }
}

return {} :: UdpModule
