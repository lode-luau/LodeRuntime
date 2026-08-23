// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include <span>
#include <string_view>
#include <cstdint>
#include <vector>
#include <memory>

struct lua_State;

namespace Lode
{

class State;
class Value;
namespace Detail { struct PinnedRef; }

/**
 * @brief Represents a Luau buffer object.
 * 
 * Provides methods mirroring Luau's standard buffer library for reading and 
 * writing native data types directly without additional allocations, utilizing 
 * zero-copy spans.
 */
class LODE_API Buffer
{
public:
    Buffer();
    Buffer(lua_State* L, int index);
    
    ~Buffer();
    Buffer(const Buffer& other);
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(const Buffer& other);
    Buffer& operator=(Buffer&& other) noexcept;

    /** @brief Checks if this object points to a valid Luau buffer. */
    [[nodiscard]] bool IsValid() const;

    /** @brief Returns a pointer to the raw buffer data. */
    [[nodiscard]] void* Data() const;

    /** @brief Returns the size of the buffer in bytes. */
    [[nodiscard]] size_t Size() const;

    /** @brief Returns a zero-copy span of the buffer data. */
    [[nodiscard]] std::span<uint8_t> Span() const;

    // --- Luau standard buffer library methods ---

    [[nodiscard]] int8_t ReadInt8(size_t offset) const;
    [[nodiscard]] uint8_t ReadUInt8(size_t offset) const;
    [[nodiscard]] int16_t ReadInt16(size_t offset) const;
    [[nodiscard]] uint16_t ReadUInt16(size_t offset) const;
    [[nodiscard]] int32_t ReadInt32(size_t offset) const;
    [[nodiscard]] uint32_t ReadUInt32(size_t offset) const;
    [[nodiscard]] float ReadFloat32(size_t offset) const;
    [[nodiscard]] double ReadFloat64(size_t offset) const;
    [[nodiscard]] std::string_view ReadString(size_t offset, size_t count) const;

    void WriteInt8(size_t offset, int8_t value);
    void WriteUInt8(size_t offset, uint8_t value);
    void WriteInt16(size_t offset, int16_t value);
    void WriteUInt16(size_t offset, uint16_t value);
    void WriteInt32(size_t offset, int32_t value);
    void WriteUInt32(size_t offset, uint32_t value);
    void WriteFloat32(size_t offset, float value);
    void WriteFloat64(size_t offset, double value);
    void WriteString(size_t offset, std::string_view value);

    /** @brief Fills a section of the buffer with a specific value. */
    void Fill(size_t offset, uint8_t value, size_t count);

    /** @brief Copies data from a source buffer into this buffer. */
    void CopyFrom(size_t targetOffset, const Buffer& source, size_t sourceOffset, size_t count);

    // Internal
    void PushToLuaState(lua_State* L) const;
    [[nodiscard]] lua_State* GetLuaState() const;

private:
    std::shared_ptr<Detail::PinnedRef> ref_;

    // Cached data pointer/size captured at construction. The data block of a
    // Luau buffer is stable for as long as the object stays pinned by ref_,
    // so accessors can skip the registry roundtrip entirely.
    uint8_t* cachedData_ = nullptr;
    size_t cachedSize_ = 0;
    bool hasCache_ = false;
};

} // namespace Lode
