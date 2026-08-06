// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Buffer.hpp"
#include "lua.h"
#include <array>
#include <bit>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include "StateLifetime.hpp"

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
}

Buffer::Buffer() = default;

Buffer::Buffer(lua_State* L, int index)
{
    if (lua_type(L, index) == LUA_TBUFFER)
    {
        L_ = lua_mainthread(L);
        lifetime_ = Detail::GetStateLifetime(L);
        lua_pushvalue(L, index);
        refId_ = lua_ref(L, -1);
        lua_pop(L, 1);
    }
}

Buffer::~Buffer()
{
    if (L_ && lifetime_ && lifetime_->alive.load() && refId_ != LUA_NOREF && refId_ != LUA_REFNIL)
    {
        lua_unref(L_, refId_);
    }
}

Buffer::Buffer(const Buffer& other)
{
    if (other.IsValid())
    {
        L_ = other.L_;
        lifetime_ = other.lifetime_;
        lua_getref(L_, other.refId_);
        refId_ = lua_ref(L_, -1);
        lua_pop(L_, 1);
    }
}

Buffer::Buffer(Buffer&& other) noexcept : L_(other.L_), refId_(other.refId_)
{
    lifetime_ = std::move(other.lifetime_);
    other.L_ = nullptr;
    other.refId_ = LUA_NOREF;
}

Buffer& Buffer::operator=(const Buffer& other)
{
    if (this != &other)
    {
        if (L_ && lifetime_ && lifetime_->alive.load() && refId_ != LUA_NOREF && refId_ != LUA_REFNIL)
        {
            lua_unref(L_, refId_);
        }
        if (other.IsValid())
        {
            L_ = other.L_;
            lifetime_ = other.lifetime_;
            lua_getref(L_, other.refId_);
            refId_ = lua_ref(L_, -1);
            lua_pop(L_, 1);
        }
        else
        {
            L_ = nullptr;
            refId_ = LUA_NOREF;
            lifetime_.reset();
        }
    }
    return *this;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        if (L_ && lifetime_ && lifetime_->alive.load() && refId_ != LUA_NOREF && refId_ != LUA_REFNIL)
        {
            lua_unref(L_, refId_);
        }
        L_ = other.L_;
        refId_ = other.refId_;
        lifetime_ = std::move(other.lifetime_);
        other.L_ = nullptr;
        other.refId_ = LUA_NOREF;
    }
    return *this;
}

bool Buffer::IsValid() const
{
    return L_ != nullptr && refId_ != LUA_NOREF && refId_ != LUA_REFNIL;
}

void* Buffer::Data() const
{
    if (!IsValid()) return nullptr;
    lua_getref(L_, refId_);
    void* ptr = lua_tobuffer(L_, -1, nullptr);
    lua_pop(L_, 1);
    return ptr;
}

size_t Buffer::Size() const
{
    if (!IsValid()) return 0;
    lua_getref(L_, refId_);
    size_t size = 0;
    lua_tobuffer(L_, -1, &size);
    lua_pop(L_, 1);
    return size;
}

std::span<uint8_t> Buffer::Span() const
{
    if (!IsValid()) return {};
    lua_getref(L_, refId_);
    size_t size = 0;
    void* ptr = lua_tobuffer(L_, -1, &size);
    lua_pop(L_, 1);
    return std::span<uint8_t>(static_cast<uint8_t*>(ptr), size);
}

void Buffer::PushToLuaState(lua_State* L) const
{
    if (!L)
        return;

    if (IsValid() && lua_mainthread(L) == L_)
    {
        lua_getref(L_, refId_);
        if (L != L_)
            lua_xmove(L_, L, 1);
    }
    else
    {
        lua_pushnil(L);
    }
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
    auto span = Span();
    if (offset >= span.size()) return;
    size_t available = span.size() - offset;
    size_t writeCount = (value.size() > available) ? available : value.size();
    if (writeCount > 0)
    {
        std::memcpy(span.data() + offset, value.data(), writeCount);
    }
}

void Buffer::Fill(size_t offset, uint8_t value, size_t count)
{
    auto span = Span();
    if (offset >= span.size()) return;
    size_t available = span.size() - offset;
    size_t fillCount = (count > available) ? available : count;
    if (fillCount > 0)
    {
        std::memset(span.data() + offset, value, fillCount);
    }
}

void Buffer::CopyFrom(size_t targetOffset, const Buffer& source, size_t sourceOffset, size_t count)
{
    auto targetSpan = Span();
    auto sourceSpan = source.Span();
    
    if (targetOffset >= targetSpan.size() || sourceOffset >= sourceSpan.size()) return;
    
    size_t targetAvailable = targetSpan.size() - targetOffset;
    size_t sourceAvailable = sourceSpan.size() - sourceOffset;
    
    size_t copyCount = count;
    if (copyCount > sourceAvailable) copyCount = sourceAvailable;
    if (copyCount > targetAvailable) copyCount = targetAvailable;
    
    if (copyCount > 0)
    {
        std::memmove(targetSpan.data() + targetOffset, sourceSpan.data() + sourceOffset, copyCount);
    }
}

} // namespace Lode
