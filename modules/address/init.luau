--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents an opaque native address. It is never a numeric address.
export type Pointer = unknown

--- Represents a registered FFI typed value accepted by Address.Of.
export type TypedValue = unknown

--- Represents an explicit retention of an owner while native code keeps an address.
export type Lease = {
    Pointer: Pointer?,
    Close: () -> (),
}

--- Provides specialized scalar reads from an opaque address.
export type AddressReader = {
    I8: (pointer: Pointer, offset: number?) -> number,
    U8: (pointer: Pointer, offset: number?) -> number,
    I16: (pointer: Pointer, offset: number?) -> number,
    U16: (pointer: Pointer, offset: number?) -> number,
    I32: (pointer: Pointer, offset: number?) -> number,
    U32: (pointer: Pointer, offset: number?) -> number,
    I64: (pointer: Pointer, offset: number?) -> number,
    U64: (pointer: Pointer, offset: number?) -> number,
    F32: (pointer: Pointer, offset: number?) -> number,
    F64: (pointer: Pointer, offset: number?) -> number,
    Bool: (pointer: Pointer, offset: number?) -> boolean,
    Pointer: (pointer: Pointer, offset: number?) -> Pointer,
}

--- Provides specialized scalar writes to an opaque address.
export type AddressWriter = {
    I8: (pointer: Pointer, offset: number, value: number) -> Pointer,
    U8: (pointer: Pointer, offset: number, value: number) -> Pointer,
    I16: (pointer: Pointer, offset: number, value: number) -> Pointer,
    U16: (pointer: Pointer, offset: number, value: number) -> Pointer,
    I32: (pointer: Pointer, offset: number, value: number) -> Pointer,
    U32: (pointer: Pointer, offset: number, value: number) -> Pointer,
    I64: (pointer: Pointer, offset: number, value: number) -> Pointer,
    U64: (pointer: Pointer, offset: number, value: number) -> Pointer,
    F32: (pointer: Pointer, offset: number, value: number) -> Pointer,
    F64: (pointer: Pointer, offset: number, value: number) -> Pointer,
    Bool: (pointer: Pointer, offset: number, value: boolean | number) -> Pointer,
    Pointer: (pointer: Pointer, offset: number, value: Pointer) -> Pointer,
}

--- Opaque pointer conversion, scalar access, and raw memory operations.
--- Foreign-memory bounds and lifetime cannot be inferred and remain caller-owned.
export type AddressModule = {
    --- Obtains an address from native-backed storage or an existing pointer.
    --- @param value Buffer, typed value, callback, memory block, lease, or pointer.
    --- @param byteOffset Optional signed byte offset.
    --- @return The opaque address.
    Of: (value: buffer | TypedValue | Pointer, byteOffset: number?) -> Pointer,

    --- Validates a pointer type declaration without changing the address.
    --- @param typeNameOrType C pointer declaration or FFI descriptor.
    --- @param pointer Address to preserve.
    --- @return The same opaque address.
    Cast: (typeNameOrType: string | unknown, pointer: Pointer) -> Pointer,

    --- Applies a signed byte offset to an opaque address.
    --- @param pointer Base address.
    --- @param byteOffset Signed byte offset.
    --- @return Offset address.
    Add: (pointer: Pointer, byteOffset: number) -> Pointer,

    --- Returns whether an address is nil.
    --- @param pointer Address to inspect.
    --- @return True when the address is nil.
    IsNull: (pointer: Pointer) -> boolean,

    --- Compares two opaque addresses without converting them to numbers.
    --- @param left First address.
    --- @param right Second address.
    --- @return True when both addresses are identical.
    Equals: (left: Pointer, right: Pointer) -> boolean,

    Read: AddressReader,
    Write: AddressWriter,

    --- Copies a non-overlapping byte range.
    --- @param destination Writable destination address.
    --- @param source Readable source address.
    --- @param bytes Number of bytes.
    Copy: (destination: Pointer, source: Pointer, bytes: number) -> (),

    --- Moves a byte range and permits overlap.
    --- @param destination Writable destination address.
    --- @param source Readable source address.
    --- @param bytes Number of bytes.
    Move: (destination: Pointer, source: Pointer, bytes: number) -> (),

    --- Fills a byte range with the low byte of a number.
    --- @param destination Writable destination address.
    --- @param byteValue Fill value.
    --- @param bytes Number of bytes.
    Fill: (destination: Pointer, byteValue: number, bytes: number) -> (),

    --- Copies a NUL-terminated C string or an explicit byte range.
    --- @param pointer Readable character address.
    --- @param bytes Optional exact byte count.
    --- @return A Luau string copy.
    String: (pointer: Pointer, bytes: number?) -> string,

    --- Converts a UTF-16 native string on supported platforms.
    WString: (pointer: Pointer, codeUnits: number?) -> string,

    --- Encodes a Luau string as a NUL-terminated UTF-16 buffer on supported platforms.
    WBuffer: (text: string) -> buffer,

    --- Retains an owner explicitly while C holds an address derived from it.
    --- @param owner Value that owns the address.
    --- @return A lease; close it only after C releases the address.
    Pin: (owner: unknown) -> Lease,

    --- Retains an owner for an address obtained separately.
    --- @param owner Value that owns the address.
    --- @param pointer Address retained by C.
    --- @return A lease; close it only after C releases the address.
    KeepAlive: (owner: unknown, pointer: Pointer) -> Lease,
}

return {} :: AddressModule
