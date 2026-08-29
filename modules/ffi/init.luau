--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents an opaque native address. It is not a numeric address.
export type Pointer = unknown

--- Represents a C type descriptor returned by FFI.TypeOf.
export type CType = unknown

--- Represents a registered C struct or union value.
export type TypedValue = unknown

--- Represents the dynamically determined result of an FFI call.
---
--- A C declaration is parsed only at runtime, so Luau cannot statically know
--- the result shape of a symbol selected from a dynamic library. This is
--- intentionally `any`: it keeps ordinary dynamic bindings ergonomic. Use
--- Library<T> when a binding needs static checking.
export type FFIResult = any

--- Represents counters and closure state for one callback.
export type CallbackState = {
    closed: boolean,
    --- True after the first Luau exception; subsequent calls return ABI zero.
    faulted: boolean,
    --- Number of callback entries currently executing (all threads).
    activeCalls: number,
    --- Nested callback depth on the runtime owner thread. Re-entrant entries
    --- are allowed and remain synchronous; this is diagnostic only.
    callbackDepth: number,
    foreignCalls: number,
    postedForeignCalls: number,
    rejectedForeignCalls: number,
    --- The first callback exception, or an empty string when none occurred.
    lastError: string,
}

--- Represents a C-callable callback closure.
export type Callback = {
    Pointer: Pointer,
    --- Closes after C has released the pointer. Calling it from the callback
    --- itself defers native closure reclamation until the active call returns.
    Close: () -> (),
    State: () -> CallbackState,
    ForeignCalls: () -> number,
}

--- Controls how a callback handles calls from a foreign thread.
export type CallbackOptions = {
    ForeignThread: "post"?,
}

--- Controls one dynamic-library load.
export type LoadOptions = {
    SearchDllDirectory: boolean?,
}

--- Represents one C function whose precise signature is known at runtime.
--- Arguments and results are dynamic at this boundary; C ABI validation still
--- happens in the native binding before ffi_call.
export type FFIFunction = (...any) -> FFIResult

--- Represents the lifecycle operations shared by every loaded library.
export type LibraryHandle = {
    --- Idempotently releases the library handle after active calls finish.
    Close: () -> (),
    --- Calls a declared symbol through the asynchronous dispatcher.
    CallAsync: (name: string, ...any) -> FFIResult,
}

--- Represents a loaded library with a caller-declared symbol surface.
---
--- C declarations are runtime strings, so Luau cannot derive a symbol's
--- parameter and return types from FFI.Load. Declare them in a type and apply
--- a type-only cast at the load site; this has no runtime cost:
---
--- ```luau
--- type Kernel32 = {
---     GetCurrentProcessId: () -> number,
--- }
--- local kernel32 = FFI.Load("kernel32", "unsigned long GetCurrentProcessId(void);")
---     :: FFI.Library<Kernel32>
--- assert(kernel32.GetCurrentProcessId() > 0)
--- ```
export type Library<T> = T & LibraryHandle

--- Represents a library whose symbols have not yet been given Luau types.
export type DynamicLibrary = {
    [string]: FFIFunction,
    Close: () -> (),
    CallAsync: (name: string, ...any) -> FFIResult,
}

--- Foreign Function Interface module.
---
--- Contract errors raised by this module are catchable with pcall. Pointer
--- validity, foreign-memory bounds, ABI correctness and faults inside C code
--- remain the caller's responsibility.
export type FFIModule = {
    --- Registers aliases, aggregates and function-pointer types for this State.
    --- @param declarations C declarations to register.
    CDef: (declarations: string) -> (),

    --- Resolves a registered or built-in C type once for later reuse.
    --- @param typeName Registered or built-in C type name.
    --- @return A reusable C type descriptor.
    TypeOf: (typeName: string) -> CType,

    --- Returns the ABI size of a C type or typed aggregate value.
    --- @param typeNameOrValue C type name, descriptor, or typed value.
    --- @return The type size in bytes.
    SizeOf: (typeNameOrValue: string | CType | TypedValue) -> number,

    --- Returns the ABI alignment requirement of a C type.
    --- @param typeNameOrType C type name or descriptor.
    --- @return The alignment in bytes.
    AlignOf: (typeNameOrType: string | CType) -> number,

    --- Returns the byte offset of a named aggregate field.
    --- @param typeNameOrType Registered aggregate name or descriptor.
    --- @param fieldName Declared field name.
    --- @return The field offset in bytes.
    OffsetOf: (typeNameOrType: string | CType, fieldName: string) -> number,

    --- Allocates raw storage or creates a registered typed aggregate.
    --- @param typeNameOrSize Byte count, C type name, or C type descriptor.
    --- @param initializer Optional named field values for a typed aggregate.
    --- @return Luau buffer storage or a typed aggregate value.
    new: (typeNameOrSize: string | number | CType, initializer: unknown?) -> buffer | TypedValue,

    --- Allocates a raw buffer using the ABI layout of field declarations.
    --- @param fields C field declarations.
    --- @return Zero-initialized raw storage.
    Struct: (fields: string) -> buffer,

    --- Allocates a raw buffer using the ABI layout of union declarations.
    --- @param fields C union member declarations.
    --- @return Zero-initialized raw storage.
    Union: (fields: string) -> buffer,

    --- Loads a dynamic library and eagerly binds its declared symbols.
    --- @param libraryName Library name or path.
    --- @param declarations Optional C function declarations.
    --- @param options Optional loader behavior.
    --- @return The loaded library handle.
    Load: (libraryName: string, declarations: string?, options: LoadOptions?) -> DynamicLibrary,

    --- Binds declarations against symbols exported by the current process.
    --- @param declarations C function declarations.
    --- @return A process-symbol library handle.
    C: (declarations: string) -> DynamicLibrary,

    --- Creates a C-callable callback pointer.
    --- Foreign-thread callbacks return zero immediately; with ForeignThread
    --- set to "post", supported arguments are queued on the runtime thread.
    --- @param declarationOrType Function declaration or registered function type.
    --- @param fn Luau implementation of the callback.
    --- @param options Foreign-thread dispatch options.
    --- @return The callback handle that must remain alive while C retains it.
    Callback: (declarationOrType: string | CType, fn: (...any) -> FFIResult, options: CallbackOptions?) -> Callback,

    --- Schedules a function on the owning runtime event-loop thread.
    --- @param fn Function to schedule.
    Post: (fn: () -> ()) -> (),

    --- Returns GetLastError on Windows or errno on POSIX from the latest FFI call.
    --- @return The captured platform error code.
    LastError: () -> number,
}

return {} :: FFIModule
