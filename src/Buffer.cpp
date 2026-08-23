// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Buffer.hpp"
#include "PinnedRef.hpp"
#include "lua.h"
#include <array>
#include <bit>
#include <cstring>
#include <algorithm>

namespace Lode
{

namespace
{
    bool HasRange(std::span<uint8_t> span, size_t offset, size_t width)
    {
        return offset <= span.size() && width <= span.size() - offset;
    }

    template <typename T>
    T ReadLittleEndian(std::span<uint8_t> span, size_t offset)
    {
        std::array<uint8_t, sizeof(T)> bytes{};
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = span[offset + i];
        if constexpr (std::endian::native == std::endian::big)
            std::reverse(bytes.begin(), bytes.end());

        T value{};
        std::memcpy(&value, bytes.data(), sizeof(value));
        return value;
    }

    template <typename T>
    void WriteLittleEndian(std::span<uint8_t> span, size_t offset, T value)
    {
        std::array<uint8_t, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(value));
        if constexpr (std::endian::native == std::endian::big)
            std::reverse(bytes.begin(), bytes.end());
        for (size_t i = 0; i < bytes.size(); ++i)
            span[offset + i] = bytes[i];
    }

    // Clamps a request for `count` bytes at `offset` to the span's bounds.
    std::span<uint8_t> ClampRange(std::span<uint8_t> span, size_t offset, size_t count)
    {
        if (offset >= span.size())
            return {};
        size_t available = span.size() - offset;
        if (count > available)
            count = available;
        return span.subspan(offset, count);
    }
}

Buffer::Buffer() = default;

Buffer::Buffer(lua_State* L, int index)
{
    if (lua_type(L, index) == LUA_TBUFFER)
    {
        ref_ = std::make_shared<Detail::PinnedRef>(Detail::CaptureRef(L, index));

        // The value is on the stack here, so resolve pointer/size without a
        // registry roundtrip. Both stay valid while ref_ keeps the buffer
        // pinned, letting every accessor skip lua_getref/lua_pop entirely.
        size_t size = 0;
        void* data = lua_tobuffer(L, index, &size);
        if (data != nullptr || size != 0)
        {
            cachedData_ = static_cast<uint8_t*>(data);
            cachedSize_ = size;
            hasCache_ = true;
        }
    }
}

Buffer::~Buffer() = default;
Buffer::Buffer(const Buffer& other) = default;
Buffer::Buffer(Buffer&& other) noexcept = default;
Buffer& Buffer::operator=(const Buffer& other) = default;
Buffer& Buffer::operator=(Buffer&& other) noexcept = default;

bool Buffer::IsValid() const
{
    return ref_ && ref_->L && ref_->refId != LUA_NOREF && ref_->refId != LUA_REFNIL;
}

void* Buffer::Data() const
{
    if (!IsValid()) return nullptr;
    if (hasCache_) return cachedData_;
    lua_getref(ref_->L, ref_->refId);
    void* ptr = lua_tobuffer(ref_->L, -1, nullptr);
    lua_pop(ref_->L, 1);
    return ptr;
}

size_t Buffer::Size() const
{
    if (!IsValid()) return 0;
    if (hasCache_) return cachedSize_;
    lua_getref(ref_->L, ref_->refId);
    size_t size = 0;
    lua_tobuffer(ref_->L, -1, &size);
    lua_pop(ref_->L, 1);
    return size;
}

std::span<uint8_t> Buffer::Span() const
{
    if (!IsValid()) return {};
    if (hasCache_) return std::span<uint8_t>(cachedData_, cachedSize_);
    lua_getref(ref_->L, ref_->refId);
    size_t size = 0;
    void* ptr = lua_tobuffer(ref_->L, -1, &size);
    lua_pop(ref_->L, 1);
    return std::span<uint8_t>(static_cast<uint8_t*>(ptr), size);
}

lua_State* Buffer::GetLuaState() const
{
    return ref_ ? ref_->L : nullptr;
}

void Buffer::PushToLuaState(lua_State* L) const
{
    if (!L)
        return;

    if (ref_)
        Detail::PushRef(L, *ref_);
    else
        lua_pushnil(L);
}

// --- Reads ---

int8_t Buffer::ReadInt8(size_t offset) const
{
    auto span = Span();
    if (offset >= span.size()) return 0;
    return static_cast<int8_t>(span[offset]);
}

uint8_t Buffer::ReadUInt8(size_t offset) const
{
    auto span = Span();
    if (offset >= span.size()) return 0;
    return span[offset];
}

int16_t Buffer::ReadInt16(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(int16_t))) return 0;
    return ReadLittleEndian<int16_t>(span, offset);
}

uint16_t Buffer::ReadUInt16(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(uint16_t))) return 0;
    return ReadLittleEndian<uint16_t>(span, offset);
}

int32_t Buffer::ReadInt32(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(int32_t))) return 0;
    return ReadLittleEndian<int32_t>(span, offset);
}

uint32_t Buffer::ReadUInt32(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(uint32_t))) return 0;
    return ReadLittleEndian<uint32_t>(span, offset);
}

float Buffer::ReadFloat32(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(float))) return 0.0f;
    return ReadLittleEndian<float>(span, offset);
}

double Buffer::ReadFloat64(size_t offset) const
{
    auto span = Span();
    if (!HasRange(span, offset, sizeof(double))) return 0.0;
    return ReadLittleEndian<double>(span, offset);
}

std::string_view Buffer::ReadString(size_t offset, size_t count) const
{
    auto span = Span();
    if (offset >= span.size()) return std::string_view();
    size_t available = span.size() - offset;
    size_t actualCount = (count > available) ? available : count;
    return std::string_view(reinterpret_cast<const char*>(span.data() + offset), actualCount);
}

// --- Writes ---

void Buffer::WriteInt8(size_t offset, int8_t value)
{
    auto span = Span();
    if (offset < span.size()) span[offset] = static_cast<uint8_t>(value);
}

void Buffer::WriteUInt8(size_t offset, uint8_t value)
{
    auto span = Span();
    if (offset < span.size()) span[offset] = value;
}

void Buffer::WriteInt16(size_t offset, int16_t value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(int16_t)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteUInt16(size_t offset, uint16_t value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(uint16_t)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteInt32(size_t offset, int32_t value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(int32_t)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteUInt32(size_t offset, uint32_t value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(uint32_t)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteFloat32(size_t offset, float value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(float)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteFloat64(size_t offset, double value)
{
    auto span = Span();
    if (HasRange(span, offset, sizeof(double)))
    {
        WriteLittleEndian(span, offset, value);
    }
}

void Buffer::WriteString(size_t offset, std::string_view value)
{
    auto range = ClampRange(Span(), offset, value.size());
    if (!range.empty())
        std::memcpy(range.data(), value.data(), range.size());
}

void Buffer::Fill(size_t offset, uint8_t value, size_t count)
{
    auto range = ClampRange(Span(), offset, count);
    if (!range.empty())
        std::memset(range.data(), value, range.size());
}

void Buffer::CopyFrom(size_t targetOffset, const Buffer& source, size_t sourceOffset, size_t count)
{
    auto target = ClampRange(Span(), targetOffset, count);
    auto sourceRange = ClampRange(source.Span(), sourceOffset, count);
    size_t copyCount = std::min(target.size(), sourceRange.size());
    if (copyCount > 0)
    {
        std::memmove(target.data(), sourceRange.data(), copyCount);
    }
}

} // namespace Lode
