--!strict

-- Type definitions for every value exported by the native DLL. The runtime
-- loads the implementation from package.luau, while this file provides LSP
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

export type CallResult = {
    ok: boolean,
    error: string,
}

export type NativeModule = {
    greet: (name: string?) -> string,
    square: (x: number) -> number,
    fastNoYield: (x: number) -> number,
    yieldFromNoYield: () -> (),
    processBuffer: (buf: buffer, str: string) -> number,
    roundTripReference: (value: any) -> any,
    roundTripString: (value: string) -> string,
    roundTripInteger: (value: number) -> number,
    makeLargeInteger: () -> number,
    makeVector: () -> vector,
    roundTripVector: (value: vector) -> vector,
    modernObject: any,
    bufferEndianProbe: () -> { written: string, readInt: number, readFloat: number },
    bufferBoundsProbe: (buf: buffer) -> number,
    --- Copies bytes over themselves inside a scratch buffer and returns the
    --- resulting contents (overlapping-copy regression probe).
    bufferOverlapProbe: () -> string,
    --- Calls `callable` with the remaining arguments through the Value call
    --- API; raises if the call fails.
    callCallable: (callable: any, ...any) -> any,
    --- Attempts to call `value` without raising; reports the outcome.
    callNonCallable: (value: any?) -> CallResult,
    --- Always raises a raw C++ exception from inside an export callback
    --- (used to prove the runtime survives it).
    throwRaw: () -> (),
    --- Table whose __index metamethod always throws.
    throwingObject: any,
    throwCpp: () -> (),
    getSystemInfo: () -> { platform: string, arch: string },
    utils: NativeUtils,
    sibling: SiblingModule,
    onTick: TickSignal,
    --- Fires the onTick signal immediately with an optional message.
    fireOnTick: (message: string?) -> (),
    Vector3: Vector3Statics,
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string
}

return {} :: NativeModule, {} :: ModuleMetaInfo
