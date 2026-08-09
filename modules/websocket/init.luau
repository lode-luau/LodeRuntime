--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module websocket
--- Native WebSocket (RFC 6455) module. All network I/O is asynchronous and
--- runs on the LibUV event loop. Events are Lode::Signal proxies: use
--- `Connect`/`Once` for push-style callbacks, or `Wait(timeout)` for pull-style
--- reads that yield.
---
--- Only the `ws://` scheme is supported. `wss://` (TLS) is not available yet
--- and raises an error with the `tls` kind, matching the `http` module.

export type SignalConnection = {
    Connected: boolean,
    Disconnect: (self: SignalConnection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> SignalConnection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

export type WebSocketState = "connecting" | "open" | "closing" | "closed"

export type WebSocketAddress = {
    host: string,
    port: number,
}

export type ConnectOptions = {
    headers: { [string]: string }?,
    timeout: number?,
}

export type WebSocketOptions = {
    -- Reserved for future client configuration.
}

export type ServerOptions = {
    backlog: number?,
}

export type WebSocket = {
    -- Events
    Connected: Signal<()>,
    MessageReceived: Signal<string>,
    Disconnected: Signal<number?, string?>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Connect: (self: WebSocket, url: string, opts: ConnectOptions?) -> (),
    Send: (self: WebSocket, data: string | buffer) -> (),
    Ping: (self: WebSocket, data: string | buffer?) -> (),
    Close: (self: WebSocket, code: number?, reason: string?) -> (),
    Destroy: (self: WebSocket) -> (),

    -- State (read-only)
    IsConnected: (self: WebSocket) -> boolean,
    ReadyState: (self: WebSocket) -> WebSocketState,
}

export type WebSocketServer = {
    -- Events
    ClientConnected: Signal<WebSocket>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Listen: (self: WebSocketServer, port: number, host: string?) -> (),
    Close: (self: WebSocketServer) -> (),
    Destroy: (self: WebSocketServer) -> (),

    -- State (read-only)
    IsListening: (self: WebSocketServer) -> boolean,
    LocalAddress: (self: WebSocketServer) -> WebSocketAddress,
}

export type WebSocketModule = {
    WebSocket: { Create: (self: any, opts: WebSocketOptions?) -> WebSocket },
    WebSocketServer: { Create: (self: any, opts: ServerOptions?) -> WebSocketServer },
}

return {} :: WebSocketModule
