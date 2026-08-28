// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace lodeffi
{
inline constexpr uint64_t kMemoryBlockMagic = 0x4c4f44454d454d31ull;
inline constexpr uint64_t kMemoryLeaseMagic = 0x4c4f44454c454153ull;

struct MemoryControl
{
    void* data = nullptr;
    size_t size = 0;
    size_t alignment = alignof(std::max_align_t);
    uint64_t generation = 1;
    bool alive = false;

    ~MemoryControl() { Free(); }

    void Free()
    {
        if (data != nullptr)
            ::operator delete(data, std::align_val_t(alignment));
        data = nullptr;
        size = 0;
        alive = false;
        ++generation;
    }
};

struct MemoryBlockUserdata
{
    uint64_t magic = kMemoryBlockMagic;
    std::shared_ptr<MemoryControl> control;
};

// The owner Value is intentionally stored by the address module.  The native
// memory control is shared so a lease can cheaply observe Free/Resize without
// putting metadata on the lightuserdata pointer itself.
struct MemoryLeaseHeader
{
    uint64_t magic = kMemoryLeaseMagic;
    std::shared_ptr<MemoryControl> control;
    uint64_t generation = 0;
    void* pointer = nullptr;
    bool closed = false;
};

inline bool IsPowerOfTwo(size_t value) { return value != 0 && (value & (value - 1)) == 0; }
} // namespace lodeffi
