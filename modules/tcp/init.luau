--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module tcp
--- Native TCP module.

export type SignalConnection = {
    Connected: boolean,
    Disconnect: (self: SignalConnection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

export type Address = {
    host: string,
    port: number
}

export type TcpSocket = {
    -- Events
    Connected: Signal<()>,
    MessageReceived: Signal<string>,
    Disconnected: Signal<string?>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Connect: (self: TcpSocket, host: string, port: number, timeoutMs: number?) -> (),
    Send: (self: TcpSocket, data: string | buffer) -> (),
    Close: (self: TcpSocket) -> (),
    Destroy: (self: TcpSocket) -> (),
    
    -- State
    IsConnected: (self: TcpSocket) -> boolean,
    LocalAddress: (self: TcpSocket) -> Address,
    RemoteAddress: (self: TcpSocket) -> Address,
}

export type ServerOptions = {
    backlog: number?
}

export type Server = {
    -- Events
    ClientConnected: Signal<TcpSocket>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Listen: (self: Server, port: number, host: string?) -> (),
    Close: (self: Server) -> (),
    Destroy: (self: Server) -> (),
    
    -- State
    IsListening: (self: Server) -> boolean,
    LocalAddress: (self: Server) -> Address,
}

export type TcpModule = {
    Server: {
        Create: (self: any, opts: ServerOptions?) -> Server
    },
    TcpSocket: {
        Create: (self: any) -> TcpSocket
    },
    resolve: (host: string) -> string,
    resolveAsync: (host: string, callback: (address: string?, error: string?) -> ()) -> ()
}

return {} :: TcpModule
