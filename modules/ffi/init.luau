--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- A C declaration block accepted by `ffi.load` and `ffi.C`.
---
--- Supported: scalar types, pointers (including opaque `struct Name*`,
--- `union Name*`, and `typedef struct Name Name; Name*`), simple typedefs,
--- C enums, and fixed array parameters.
--- Defined `typedef struct { scalar fields } Name` values may be passed from
--- a Luau buffer. A struct return is also materialized as a new Luau buffer.
--- Callback parameters are supported; varargs
--- are deliberately unsupported.
export type Cdef = string

--- An opaque native address. Luau cannot inspect or perform arithmetic on it;
--- use `ffi.cast`, `ffi.addr`, `ffi.read`, and `ffi.write` instead.
export type Pointer = unknown

--- A registered C struct value. Field names and exact field types are known
--- only from the `ffi.cdef` declaration at runtime.
export type TypedStruct = {[string]: unknown}

--- A diagnostic snapshot for one callback handle.
export type CallbackState = {
    closed: boolean,
    foreignCalls: number,
    postedForeignCalls: number,
    rejectedForeignCalls: number,
}

export type Callback = {
    pointer: Pointer,
    close: () -> (),
    Close: () -> (),
    foreignCalls: () -> number,
    state: () -> CallbackState,
}

export type CallbackOptions = {
    --- Posts foreign-thread calls to the event loop and returns zero to C.
    foreignThread: "post"?,
}

export type StructInitializer = {[string]: number | boolean | string | buffer | Pointer?}

--- Input accepted by legacy raw-pointer helpers. Prefer the strict `Pointer`
--- APIs for new code; this alias remains broad for backward compatibility.
export type LegacyPointerInput = unknown

--- Result of a dynamically declared C call. Refine it at the call site using
--- the declaration's C return type.
export type FFIResult = unknown

--- A function produced from one declaration in a `Cdef` block.
---
--- The exact Luau signature is only known at runtime, so calls are typed as
--- `unknown`. Arity and the supported runtime argument categories are checked by
--- the native module before every call.
export type FFIFunction = (...unknown) -> FFIResult

--- Options controlling one `ffi.load` operation.
export type LoadOptions = {
    --- Windows only. Requires an absolute library path. Dependencies are
    --- resolved relative to that DLL for this load only; the process-wide DLL
    --- search policy is unchanged.
    searchDllDirectory: boolean?,
}

--- A library handle whose declared symbols are exposed as fields.
---
--- A handle owns no Luau-managed representation of C allocations. Pointers
--- returned by a function must be released using that API's documented
--- destructor (for example, the same library's declared `free` function).
export type FFILibrary = {
    [string]: FFIFunction,

    --- Requests closure of this handle without unloading it unsafely.
    ---
    --- Bound functions and in-flight `callAsync` calls keep the OS module
    --- loaded until they are collected/completed; a previously bound function
    --- therefore remains callable after `Close()`.
    Close: () -> (),

    --- Lowercase alias of `Close`.
    close: () -> (),

    --- Invokes a declared function on libuv's worker pool and yields the
    --- current coroutine until it completes.
    ---
    --- Strings and buffers are copied before the worker starts. Raw pointer
    --- addresses remain caller-owned and must stay valid until completion.
    --- `name` must be a symbol declared in this library handle.
    callAsync: (name: string, ...unknown) -> FFIResult,
}

--[=[
    FFI Module (ffi).

    Calls C functions exported by dynamic libraries through libffi. A cdef
    declaration is parsed and resolved once at load time; every call reuses
    its prepared ABI plan.

    Pointer and string lifetime:
      * nil is passed as NULL.
      * A synchronous string argument is copied and NUL-terminated for the
        duration of the call.
      * A synchronous buffer is passed as a zero-copy pointer for the call.
      * callAsync copies strings and buffers; numeric/lightuserdata pointers
        remain caller-owned until the asynchronous call completes.
      * A non-NULL pointer result is lightuserdata. Do not dereference, retain
        after its owner is released, or free it with an unrelated allocator.
      * `ffi.string` and `ffi.copy` dereference raw pointers directly. Their
        address validity and requested byte count are the caller's contract.

    Numeric policy:
      * C `bool` / `_Bool` values are represented as numbers `0` or `1`.
      * int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t,
        uint64_t, size_t and their usual C spellings preserve their declared
        ABI width.
      * Integer arguments are passed directly at their declared ABI width.
        The caller is responsible for supplying values that fit the C type.
      * int64_t and uint64_t results outside +/- (2^53 - 1) are returned as
        native-endian 8-byte buffers; pass that buffer back to a 64-bit C
        parameter without precision loss.

    Not supported in v1:
      * C varargs.
]=]
export type FFIModule = {
    --- Loads a dynamic library and eagerly binds every declaration.
    ---
    --- `declarations` can be omitted to obtain a handle with only lifecycle
    --- methods. On Windows, the `.dll` suffix is optional under normal loader
    --- rules. Use `options.searchDllDirectory` only for an absolute DLL path
    --- whose dependencies live beside it.
    ---
    --- @param name Dynamic-library name or path.
    --- @param declarations C declaration block to parse and bind.
    --- @param options Per-load loader options.
    --- @return A handle containing one callable field per declaration.
    load: (name: string, declarations: Cdef?, options: LoadOptions?) -> FFILibrary,

    --- Binds declarations against symbols exported by the current executable
    --- image (`GetModuleHandle(NULL)` on Windows, `dlopen(NULL)` on POSIX).
    ---
    --- This does not search every loaded module. Use `ffi.load` for symbols
    --- exported by a particular DLL/shared object.
    --- @param declarations C declaration block to parse and bind.
    --- @return A handle containing the resolved process-export symbols.
    C: (declarations: Cdef) -> FFILibrary,

    --- Registers typedef aliases, named structs/unions, and named function
    --- pointer types for this State. Compatible redeclarations are accepted;
    --- incompatible layouts or aliases fail. Registered aggregates can be
    --- constructed with `ffi.new("Type", initializer?)` and expose scalar
    --- and pointer fields. Type registrations never cross States.
    --- @param declarations Declarations to register for this State.
    cdef: (declarations: Cdef) -> (),

    --- Returns the OS thread error code captured immediately after the most
    --- recent completed FFI call. It is `GetLastError()` on Windows and errno
    --- on POSIX. Read it before making another FFI call on the same runtime
    --- thread; it is not a general exception mechanism.
    lastError: () -> number,

    --- Allocates zero-initialized raw bytes as a Luau `buffer`.
    ---
    --- The buffer is owned by Luau and may be passed directly to pointer
    --- parameters. Its byte layout is entirely controlled by the caller;
    --- this does not allocate C heap memory or attach a destructor.
    --- Pass a byte size directly, or a supported scalar/pointer C type such
    --- as `"int32_t"` or `"void*"`.
    --- @param bytes Number of bytes to allocate, or a C type string.
    --- @return A mutable, zero-initialized buffer.
    --- When `bytes` is a registered aggregate name, returns a `TypedStruct`.
    --- Otherwise returns the backward-compatible raw `buffer` allocation.
    --- @param bytes Byte size, scalar/pointer type, or registered type name.
    --- @param initializer Optional scalar/pointer field values for a typed struct.
    --- @return Raw storage or a field-accessible typed struct.
    new: (bytes: number | string, initializer: StructInitializer?) -> buffer,

    --- Returns the ABI size of a scalar/pointer type, a registered aggregate
    --- name, or a typed struct value.
    sizeof: (type: string | TypedStruct) -> number,

    --- Allocates a zero-initialized C-ABI struct layout as a Luau buffer.
    --- Each field must be a supported scalar or pointer declaration, for
    --- example `"int32_t x; uint16_t y;"`.
    struct: (fields: string) -> buffer,

    --- Allocates zero-initialized storage for a C union. Each member starts
    --- at byte offset zero. Defined unions are supported as pointer targets;
    --- passing or returning a union by value is intentionally unsupported.
    union: (fields: string) -> buffer,

    --- Returns the ABI byte offset of a named field in a struct layout.
    offsetof: (fields: string, name: string) -> number,

    --- Copies a NUL-terminated C string, or exactly `bytes` bytes, into a
    --- Luau string. The pointer must remain readable for the operation.
    string: (pointer: LegacyPointerInput, bytes: number?) -> string,

    --- Copies exactly `bytes` from a C pointer into a Luau-owned buffer.
    --- No ownership is transferred and no bounds are inferred.
    copy: (pointer: LegacyPointerInput, bytes: number) -> buffer,

    --- Windows only. Copies a UTF-16 C string into UTF-8 Luau text.
    wstring: (pointer: LegacyPointerInput, codeUnits: number?) -> string,

    --- Windows only. Encodes UTF-8 Luau text as a NUL-terminated UTF-16
    --- buffer suitable for `wchar_t*` APIs.
    wbuffer: (text: string) -> buffer,

    --- Converts a buffer, string, callback handle, or raw pointer to a raw
    --- C address and optionally applies a signed byte offset. The caller
    --- owns all lifetime and bounds guarantees.
    ptr: (value: LegacyPointerInput, byteOffset: number?) -> Pointer,

    --- Strict pointer helpers. Unlike `ptr`, these never coerce a Luau
    --- number into an address. Foreign-memory reads and writes cannot infer
    --- C allocation bounds; callers retain lifetime and bounds ownership.
    --- Returns the address of a Luau buffer, typed struct, callback, or pointer.
    --- @param value A value that owns or already represents a native address.
    --- @param byteOffset Signed byte offset from that address.
    addr: (value: buffer | TypedStruct | Callback | Pointer, byteOffset: number?) -> Pointer,
    --- Validates that `type` is a pointer declaration and preserves its address.
    cast: (type: string, pointer: Pointer) -> Pointer,
    --- Reads one scalar/pointer C value. Foreign-memory bounds are caller-owned.
    read: (pointer: Pointer, type: string, byteOffset: number?) -> unknown,
    --- Writes one scalar or pointer C value. Foreign-memory bounds are caller-owned.
    write: (pointer: Pointer, type: string, value: number | boolean | Pointer, byteOffset: number?) -> Pointer,
    --- Reads a pointer-sized field without converting it to a Luau number.
    readPointer: (pointer: Pointer, byteOffset: number?) -> Pointer,
    --- Writes an opaque pointer-sized field at a byte offset.
    writePointer: (pointer: Pointer, byteOffset: number, value: Pointer) -> Pointer,

    --- Writes `count` bytes to a raw destination with the low byte of value.
    fill: (destination: LegacyPointerInput, value: number, count: number) -> Pointer,

    --- Copies `count` bytes between raw addresses; overlapping memory is
    --- supported. Bounds and lifetime are the caller's responsibility.
    move: (destination: LegacyPointerInput, source: LegacyPointerInput, count: number) -> Pointer,

    --- Creates a C-callable pointer from a declaration and a Luau function.
    --- It may only be invoked synchronously from the runtime thread. Pointer
    --- results accept nil, lightuserdata, or a buffer. Keep the returned
    --- handle and any returned buffer alive while C retains their pointers.
    --- `callback.state()` exposes close and foreign-thread diagnostics.
    --- By default, calls from a foreign thread are rejected and return zero.
    --- With `foreignThread = "post"`, arguments are copied and the function
    --- runs later on the runtime thread; its return value is not sent to C.
    callback: (declaration: Cdef, fn: (...never) -> FFIResult, options: CallbackOptions?) -> Callback,

    --- Defers a Luau function to the owning runtime event-loop thread.
    --- This is safe to invoke from native integrations through the FFI's
    --- thread-safe dispatcher; it does not make foreign callbacks synchronous.
    post: (fn: () -> ()) -> (),

}

return {} :: FFIModule
