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

--- A function produced from one declaration in a `Cdef` block.
---
--- The exact Luau signature is only known at runtime, so calls are typed as
--- `any`. Arity and the supported runtime argument categories are checked by
--- the native module before every call.
export type FFIFunction = (...any) -> any

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
    callAsync: (name: string, ...any) -> any,
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
    new: (bytes: number | string) -> buffer,

    --- Returns the ABI size of one supported scalar or pointer C type.
    --- The type is parsed with the same rules as a function declaration.
    sizeof: (type: string) -> number,

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
    string: (pointer: any, bytes: number?) -> string,

    --- Copies exactly `bytes` from a C pointer into a Luau-owned buffer.
    --- No ownership is transferred and no bounds are inferred.
    copy: (pointer: any, bytes: number) -> buffer,

    --- Windows only. Copies a UTF-16 C string into UTF-8 Luau text.
    wstring: (pointer: any, codeUnits: number?) -> string,

    --- Windows only. Encodes UTF-8 Luau text as a NUL-terminated UTF-16
    --- buffer suitable for `wchar_t*` APIs.
    wbuffer: (text: string) -> buffer,

    --- Converts a buffer, string, callback handle, or raw pointer to a raw
    --- C address and optionally applies a signed byte offset. The caller
    --- owns all lifetime and bounds guarantees.
    ptr: (value: any, byteOffset: number?) -> any,

    --- Writes `count` bytes to a raw destination with the low byte of value.
    fill: (destination: any, value: number, count: number) -> any,

    --- Copies `count` bytes between raw addresses; overlapping memory is
    --- supported. Bounds and lifetime are the caller's responsibility.
    move: (destination: any, source: any, count: number) -> any,

    --- Creates a C-callable pointer from a declaration and a Luau function.
    --- It may only be invoked synchronously from the runtime thread. Pointer
    --- results accept nil, lightuserdata, or a buffer. Keep the returned
    --- handle and any returned buffer alive while C retains their pointers.
    callback: (declaration: Cdef, fn: (...any) -> any) -> {
        pointer: any,
        close: () -> (),
        Close: () -> (),
    },

}

return {} :: FFIModule
