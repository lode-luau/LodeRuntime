--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module udp
--- Native UDP module.

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

export type UdpSocket = {
    -- Events
    MessageReceived: Signal<string, Address>,
    ErrorOccurred: Signal<string>,

    -- Methods
    Bind: (self: UdpSocket, host: string, port: number) -> (),
    Send: (self: UdpSocket, data: string | buffer, host: string, port: number) -> (),
    Close: (self: UdpSocket) -> (),
    Destroy: (self: UdpSocket) -> (),
    
    -- State
    LocalAddress: (self: UdpSocket) -> Address,
}

export type UdpModule = {
    UdpSocket: {
        Create: (self: any) -> UdpSocket
    }
}

return {} :: UdpModule
