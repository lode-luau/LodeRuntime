--!strict

-- Type definitions for every value exported by the native DLL. The runtime
-- loads the implementation from lode.json, while this file provides LSP
-- completion and type checking for Luau callers.

export type Vector3 = {
    x: number,
    y: number,
    z: number,
    length: (self: Vector3) -> number,
}

export type Vector3Statics = {
    new: (x: number, y: number, z: number) -> Vector3
}

export type NativeUtils = {
    formatGreeting: (name: string) -> string,
    double: (n: number) -> number,
    describe: () -> string,
}

export type SiblingModule = {
    name: string,
    message: string,
    answer: number,
}

export type Connection = {
    Disconnect: (self: Connection) -> (),
}

export type TickSignal = {
    Connect: (self: TickSignal, callback: (message: string) -> ()) -> Connection,
    Once: (self: TickSignal, callback: (message: string) -> ()) -> Connection,
    Wait: (self: TickSignal, timeout: number?) -> string?,
}

export type NativeModule = {
    greet: (name: string?) -> string,
    square: (x: number) -> number,
    processBuffer: (buf: buffer, str: string) -> number,
    roundTripReference: (value: any) -> any,
    roundTripString: (value: string) -> string,
    roundTripInteger: (value: number) -> number,
    makeLargeInteger: () -> number,
    makeVector: () -> vector,
    roundTripVector: (value: vector) -> vector,
    bufferBoundsProbe: (buf: buffer) -> number,
    throwCpp: () -> (),
    getSystemInfo: () -> { platform: string, arch: string },
    utils: NativeUtils,
    sibling: SiblingModule,
    onTick: TickSignal,
    Vector3: Vector3Statics,
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string
}

return {} :: NativeModule, {} :: ModuleMetaInfo
