--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module socket
--- Native TCP socket module. All network I/O is asynchronous and runs on the
--- LibUV event loop. Events are Lode::Signal proxies: use `Connect`/`Once` for
--- push-style callbacks, or `Wait(timeout)` for pull-style reads that yield.

export type SignalConnection = {
    Connected: boolean,
    Disconnect: (self: SignalConnection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

export type SocketAddress = {
    host: string,
    port: number,
}

export type ServerOptions = {
    backlog: number?,
}

export type TcpSocket = {
    -- Events
    Connected: Signal<()>,
    MessageReceived: Signal<string>,
    Disconnected: Signal<string?>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Connect: (self: TcpSocket, host: string, port: number, timeout: number?) -> (),
    Send: (self: TcpSocket, data: string | buffer) -> (),
    Close: (self: TcpSocket) -> (),
    Destroy: (self: TcpSocket) -> (),

    -- State (read-only)
    IsConnected: (self: TcpSocket) -> boolean,
    LocalAddress: (self: TcpSocket) -> SocketAddress,
    RemoteAddress: (self: TcpSocket) -> SocketAddress,
}

export type Server = {
    -- Events
    ClientConnected: Signal<TcpSocket>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Listen: (self: Server, port: number, host: string?) -> (),
    Close: (self: Server) -> (),
    Destroy: (self: Server) -> (),

    -- State (read-only)
    IsListening: (self: Server) -> boolean,
    LocalAddress: (self: Server) -> SocketAddress,
}

export type SocketModule = {
    Server: { Create: (self: any, opts: ServerOptions?) -> Server },
    TcpSocket: { Create: (self: any) -> TcpSocket },
    resolve: (host: string) -> string,
    resolveAsync: (host: string, callback: (address: string?, error: string?) -> ()) -> (),
}

return {} :: SocketModule
