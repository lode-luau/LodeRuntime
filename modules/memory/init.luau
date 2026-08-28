--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents an opaque native address returned by Memory.Address.
export type Address = unknown

--- Represents a runtime-owned native memory allocation.
export type MemoryBlock = unknown

--- Controls allocation initialization and alignment.
export type MemoryOptions = {
    --- Initializes every allocated byte to zero when true.
    Zeroed: boolean?,
    --- Requested power-of-two alignment; omit or use zero for the runtime default.
    Alignment: number?,
}

--- Runtime-owned native memory module.
--- Addresses returned from a block are non-owning. After Resize or Free, obtain
--- a new address and keep every C lifetime explicitly with Address.Pin/KeepAlive.
export type MemoryModule = {
    --- Allocates a runtime-owned native block.
    --- @param byteCount Number of bytes to allocate.
    --- @param options Optional initialization and alignment options.
    --- @return The owned native block.
    new: (byteCount: number, options: MemoryOptions?) -> MemoryBlock,

    --- Returns the current byte size of a block.
    --- @param block Native block to inspect.
    --- @return Current number of bytes.
    Size: (block: MemoryBlock) -> number,

    --- Returns a non-owning address within a live block.
    --- @param block Native block to address.
    --- @param byteOffset Optional non-negative offset.
    --- @return The opaque address.
    Address: (block: MemoryBlock, byteOffset: number?) -> Address,

    --- Returns whether a block still owns a live allocation.
    --- @param block Native block to inspect.
    --- @return True while the block has not been freed.
    IsAlive: (block: MemoryBlock) -> boolean,

    --- Resizes a live block and preserves its common prefix.
    --- Previous addresses and leases must be discarded after this call.
    --- @param block Native block to resize.
    --- @param newByteCount New allocation size.
    Resize: (block: MemoryBlock, newByteCount: number) -> (),

    --- Fills all or part of a live block with zero bytes.
    --- @param block Native block to clear.
    --- @param byteOffset Optional starting offset.
    --- @param byteCount Optional range length.
    Zero: (block: MemoryBlock, byteOffset: number?, byteCount: number?) -> (),

    --- Idempotently releases a block and invalidates its leases.
    --- @param block Native block to release.
    Free: (block: MemoryBlock) -> (),
}

return {} :: MemoryModule
