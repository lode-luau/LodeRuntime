// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/StackValue.hpp"
#include "Lode/Table.hpp"
#include "lua.h"

#include "NativeMemory.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
enum class Scalar { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, Bool, Pointer };

struct LeaseUserdata
{
    uint64_t magic = lodeffi::kMemoryLeaseMagic;
    Lode::Value owner;
    lodeffi::MemoryBlockUserdata* memoryBlock = nullptr;
    uint64_t generation = 0;
    void* pointer = nullptr;
    bool closed = false;
};

[[noreturn]] void Error(const std::string& message) { throw std::runtime_error(message); }

size_t Offset(const Lode::StackValue& value, const char* api)
{
    if (!value.IsNumber()) Error(std::string(api) + ": offset must be a number");
    const double number = value.AsNumber();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < 0 || number > static_cast<double>(std::numeric_limits<intptr_t>::max()))
        Error(std::string(api) + ": invalid offset");
    return static_cast<size_t>(static_cast<intptr_t>(number));
}

intptr_t SignedOffset(const Lode::StackValue& value, const char* api)
{
    if (!value.IsNumber()) Error(std::string(api) + ": offset must be a number");
    const double number = value.AsNumber();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<intptr_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<intptr_t>::max()))
        Error(std::string(api) + ": invalid offset");
    return static_cast<intptr_t>(number);
}

uintptr_t AddOffset(uintptr_t base, intptr_t offset, const char* api)
{
    if (offset >= 0)
    {
        const uintptr_t amount = static_cast<uintptr_t>(offset);
        if (amount > std::numeric_limits<uintptr_t>::max() - base)
            Error(std::string(api) + ": address overflow");
        return base + amount;
    }

    // Avoid negating INT_PTR_MIN, which would overflow before the unsigned conversion.
    const uintptr_t amount = static_cast<uintptr_t>(-(offset + 1)) + 1;
    if (amount > base) Error(std::string(api) + ": address overflow");
    return base - amount;
}

void DestroyLease(void* raw) { static_cast<LeaseUserdata*>(raw)->~LeaseUserdata(); }

LeaseUserdata* LeaseFromTable(lua_State* L, int index)
{
    lua_getfield(L, index, "__addressLease");
    if (lua_type(L, -1) != LUA_TUSERDATA) { lua_pop(L, 1); return nullptr; }
    auto* lease = static_cast<LeaseUserdata*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return lease != nullptr && lease->magic == lodeffi::kMemoryLeaseMagic ? lease : nullptr;
}

lodeffi::MemoryBlockUserdata* MemoryOwner(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA) return nullptr;
    auto* block = static_cast<lodeffi::MemoryBlockUserdata*>(lua_touserdata(L, index));
    if (block != nullptr && block->magic == lodeffi::kMemoryBlockMagic) return block;
    return nullptr;
}

void* Pointer(lua_State* L, int index, const char* api)
{
    const int type = lua_type(L, index);
    if (type == LUA_TLIGHTUSERDATA)
    {
        void* pointer = lua_touserdata(L, index);
        if (pointer == nullptr) Error(std::string(api) + ": expected a non-NULL pointer");
        return pointer;
    }
    if (type == LUA_TBUFFER)
    {
        size_t size = 0;
        void* pointer = lua_tobuffer(L, index, &size);
        if (pointer == nullptr || size == 0) Error(std::string(api) + ": expected non-empty storage");
        return pointer;
    }
    if (type == LUA_TUSERDATA)
    {
        auto* block = static_cast<lodeffi::MemoryBlockUserdata*>(lua_touserdata(L, index));
        if (block != nullptr && block->magic == lodeffi::kMemoryBlockMagic && block->control && block->control->alive && block->control->data)
            return block->control->data;
    }
    if (type == LUA_TTABLE)
    {
        Lode::Value value = Lode::Value::FromLuaState(L, index);
        const Lode::Table table = value.AsTable();
        if (LeaseUserdata* lease = LeaseFromTable(L, index))
        {
            if (lease->closed) Error(std::string(api) + ": lease is closed");
            if (lease->memoryBlock && (!lease->memoryBlock->control || !lease->memoryBlock->control->alive || lease->memoryBlock->control->generation != lease->generation))
                Error(std::string(api) + ": lease is invalid after Memory.Free or Memory.Resize");
            if (lease->pointer == nullptr) Error(std::string(api) + ": lease has a NULL pointer");
            return lease->pointer;
        }
        const auto storage = table.Get("__ffiBuffer");
        if (storage.IsOk() && storage.GetValue().IsBuffer())
        {
            auto span = storage.GetValue().AsSpan();
            if (!span.empty()) return span.data();
        }
        const auto pointer = table.Get("Pointer");
        if (pointer.IsOk() && pointer.GetValue().GetType() == Lode::ValueType::LightUserdata)
        {
            void* raw = pointer.GetValue().AsLightUserdata();
            if (raw != nullptr) return raw;
        }
    }
    Error(std::string(api) + ": expected an opaque pointer, buffer, typed value, callback, or MemoryBlock");
}

void* At(lua_State* L, int pointerIndex, size_t offset, size_t alignment, const char* api)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(Pointer(L, pointerIndex, api));
    if (offset > std::numeric_limits<uintptr_t>::max() - base) Error(std::string(api) + ": address overflow");
    const uintptr_t address = base + offset;
    if (alignment > 1 && address % alignment != 0) Error(std::string(api) + ": address is not aligned");
    return reinterpret_cast<void*>(address);
}

size_t Width(Scalar scalar)
{
    switch (scalar) {
    case Scalar::I8: case Scalar::U8: case Scalar::Bool: return 1;
    case Scalar::I16: case Scalar::U16: return 2;
    case Scalar::I32: case Scalar::U32: case Scalar::F32: return 4;
    case Scalar::I64: case Scalar::U64: case Scalar::F64: return 8;
    case Scalar::Pointer: return sizeof(void*);
    }
    return 1;
}

template <typename T> T Load(void* address) { T value{}; std::memcpy(&value, address, sizeof(T)); return value; }
template <typename T> void Store(void* address, T value) { std::memcpy(address, &value, sizeof(T)); }

void PushScalar(lua_State* L, Scalar scalar, void* address)
{
    switch (scalar) {
    case Scalar::I8: lua_pushnumber(L, Load<int8_t>(address)); break;
    case Scalar::U8: lua_pushnumber(L, Load<uint8_t>(address)); break;
    case Scalar::I16: lua_pushnumber(L, Load<int16_t>(address)); break;
    case Scalar::U16: lua_pushnumber(L, Load<uint16_t>(address)); break;
    case Scalar::I32: lua_pushnumber(L, Load<int32_t>(address)); break;
    case Scalar::U32: lua_pushnumber(L, Load<uint32_t>(address)); break;
    case Scalar::I64: lua_pushnumber(L, static_cast<double>(Load<int64_t>(address))); break;
    case Scalar::U64: lua_pushnumber(L, static_cast<double>(Load<uint64_t>(address))); break;
    case Scalar::F32: lua_pushnumber(L, Load<float>(address)); break;
    case Scalar::F64: lua_pushnumber(L, Load<double>(address)); break;
    case Scalar::Bool: lua_pushboolean(L, Load<uint8_t>(address) != 0); break;
    case Scalar::Pointer: { void* value = Load<void*>(address); if (value) lua_pushlightuserdata(L, value); else lua_pushnil(L); break; }
    }
}

void WriteScalar(lua_State* L, Scalar scalar, void* address, const Lode::StackValue& value)
{
    if (scalar == Scalar::Pointer) { Store<void*>(address, Pointer(L, 3, "Address.Write.Pointer")); return; }
    if (!value.IsNumber() && !value.IsBoolean()) Error("Address.Write: value must be a number or boolean");
    const double number = value.IsBoolean() ? (value.AsBoolean() ? 1.0 : 0.0) : value.AsNumber();
    switch (scalar) {
    case Scalar::I8: Store<int8_t>(address, static_cast<int8_t>(number)); break;
    case Scalar::U8: Store<uint8_t>(address, static_cast<uint8_t>(number)); break;
    case Scalar::Bool: Store<uint8_t>(address, static_cast<uint8_t>(number != 0)); break;
    case Scalar::I16: Store<int16_t>(address, static_cast<int16_t>(number)); break;
    case Scalar::U16: Store<uint16_t>(address, static_cast<uint16_t>(number)); break;
    case Scalar::I32: Store<int32_t>(address, static_cast<int32_t>(number)); break;
    case Scalar::U32: Store<uint32_t>(address, static_cast<uint32_t>(number)); break;
    case Scalar::I64: Store<int64_t>(address, static_cast<int64_t>(number)); break;
    case Scalar::U64: Store<uint64_t>(address, static_cast<uint64_t>(number)); break;
    case Scalar::F32: Store<float>(address, static_cast<float>(number)); break;
    case Scalar::F64: Store<double>(address, number); break;
    case Scalar::Pointer: break;
    }
}

void AddRead(Lode::State& vm, Lode::Table& table, const char* name, Scalar scalar)
{
    table.Set(name, vm.CreateFastFunctionNNoYield([scalar](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() < 1 || args.Size() > 2) Error("Address.Read: expected pointer and optional offset");
        const size_t offset = args.Size() == 2 ? Offset(args[1], "Address.Read") : 0;
        PushScalar(args.RawState(), scalar, At(args.RawState(), 1, offset, Width(scalar), "Address.Read"));
        return 1;
    }));
}

void AddWrite(Lode::State& vm, Lode::Table& table, const char* name, Scalar scalar)
{
    table.Set(name, vm.CreateFastFunctionNNoYield([scalar](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 3) Error("Address.Write: expected pointer, offset, and value");
        const size_t offset = Offset(args[1], "Address.Write");
        void* pointer = At(args.RawState(), 1, offset, Width(scalar), "Address.Write");
        WriteScalar(args.RawState(), scalar, pointer, args[2]);
        lua_pushlightuserdata(args.RawState(), Pointer(args.RawState(), 1, "Address.Write"));
        return 1;
    }));
}
} // namespace

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    exports.Set("Of", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() < 1 || args.Size() > 2) Error("Address.Of: expected value and optional offset");
        const uintptr_t base = reinterpret_cast<uintptr_t>(Pointer(args.RawState(), 1, "Address.Of"));
        const intptr_t offset = args.Size() == 2 ? SignedOffset(args[1], "Address.Of") : 0;
        const uintptr_t address = AddOffset(base, offset, "Address.Of");
        if (address == 0) lua_pushnil(args.RawState()); else lua_pushlightuserdata(args.RawState(), reinterpret_cast<void*>(address));
        return 1;
    }));
    exports.Set("Cast", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 2 || (!args[0].IsString() && !args[0].IsTable())) Error("Address.Cast: expected pointer type and pointer");
        std::string type;
        if (args[0].IsString()) type = args[0].AsString();
        else
        {
            const auto name = args[0].AsTable().Get("__ffiType");
            if (name.IsError() || !name.GetValue().IsString()) Error("Address.Cast: invalid type descriptor");
            type = name.GetValue().AsString();
        }
        if (type.find('*') == std::string::npos) Error("Address.Cast: target type must be a pointer type");
        lua_pushlightuserdata(args.RawState(), Pointer(args.RawState(), 2, "Address.Cast"));
        return 1;
    }));
    exports.Set("Add", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 2) Error("Address.Add: expected pointer and offset");
        const uintptr_t base = reinterpret_cast<uintptr_t>(Pointer(args.RawState(), 1, "Address.Add"));
        const intptr_t offset = SignedOffset(args[1], "Address.Add");
        const uintptr_t address = AddOffset(base, offset, "Address.Add");
        if (address == 0) lua_pushnil(args.RawState()); else lua_pushlightuserdata(args.RawState(), reinterpret_cast<void*>(address));
        return 1;
    }));
    exports.Set("IsNull", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 1) Error("Address.IsNull: expected a pointer");
        if (args[0].IsNil()) { lua_pushboolean(args.RawState(), true); return 1; }
        (void)Pointer(args.RawState(), 1, "Address.IsNull");
        lua_pushboolean(args.RawState(), false); return 1;
    }));
    exports.Set("Equals", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 2) Error("Address.Equals: expected two pointers");
        void* left = args[0].IsNil() ? nullptr : Pointer(args.RawState(), 1, "Address.Equals");
        void* right = args[1].IsNil() ? nullptr : Pointer(args.RawState(), 2, "Address.Equals");
        lua_pushboolean(args.RawState(), left == right); return 1;
    }));

    Lode::Table read = vm.CreateTable();
    Lode::Table write = vm.CreateTable();
    for (const auto& entry : { std::pair{"I8", Scalar::I8}, {"U8", Scalar::U8}, {"I16", Scalar::I16}, {"U16", Scalar::U16}, {"I32", Scalar::I32}, {"U32", Scalar::U32}, {"I64", Scalar::I64}, {"U64", Scalar::U64}, {"F32", Scalar::F32}, {"F64", Scalar::F64}, {"Bool", Scalar::Bool}, {"Pointer", Scalar::Pointer} }) { AddRead(vm, read, entry.first, entry.second); AddWrite(vm, write, entry.first, entry.second); }
    exports.Set("Read", read); exports.Set("Write", write);
    exports.Set("Copy", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int { if (args.Size() != 3) Error("Address.Copy: expected destination, source, bytes"); std::memcpy(Pointer(args.RawState(), 1, "Address.Copy"), Pointer(args.RawState(), 2, "Address.Copy"), Offset(args[2], "Address.Copy")); return 0; }));
    exports.Set("Move", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int { if (args.Size() != 3) Error("Address.Move: expected destination, source, bytes"); std::memmove(Pointer(args.RawState(), 1, "Address.Move"), Pointer(args.RawState(), 2, "Address.Move"), Offset(args[2], "Address.Move")); return 0; }));
    exports.Set("Fill", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 3 || !args[1].IsNumber()) Error("Address.Fill: expected destination, byte, bytes");
        const double byte = args[1].AsNumber();
        if (!std::isfinite(byte) || std::floor(byte) != byte ||
            byte < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
            byte > static_cast<double>(std::numeric_limits<int64_t>::max()))
            Error("Address.Fill: byte must be a finite integer");
        std::memset(Pointer(args.RawState(), 1, "Address.Fill"), static_cast<unsigned char>(static_cast<int64_t>(byte)), Offset(args[2], "Address.Fill"));
        return 0;
    }));
    exports.Set("String", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int { if (args.Size() < 1 || args.Size() > 2) Error("Address.String: expected pointer and optional bytes"); const char* p = static_cast<const char*>(Pointer(args.RawState(), 1, "Address.String")); const size_t n = args.Size() == 2 ? Offset(args[1], "Address.String") : std::strlen(p); lua_pushlstring(args.RawState(), p, n); return 1; }));
    exports.Set("WString", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
#if defined(_WIN32)
        if (args.Size() < 1 || args.Size() > 2) Error("Address.WString: expected pointer and optional code-unit count");
        const wchar_t* wide = static_cast<const wchar_t*>(Pointer(args.RawState(), 1, "Address.WString"));
        const int units = args.Size() == 2 ? static_cast<int>(Offset(args[1], "Address.WString")) : static_cast<int>(std::wcslen(wide));
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units, nullptr, 0, nullptr, nullptr);
        if (bytes == 0 && units != 0) Error("Address.WString: invalid UTF-16 input");
        std::string text(static_cast<size_t>(bytes), '\0');
        if (bytes != 0) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units, text.data(), bytes, nullptr, nullptr);
        lua_pushlstring(args.RawState(), text.data(), text.size());
        return 1;
#else
        Error("Address.WString: UTF-16 helpers are only available on Windows");
#endif
    }));
    exports.Set("WBuffer", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
#if defined(_WIN32)
        if (args.Size() != 1 || !args[0].IsString()) Error("Address.WBuffer: expected one UTF-8 string");
        const std::string text = args[0].AsString();
        const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (units == 0 && !text.empty()) Error("Address.WBuffer: invalid UTF-8 input");
        void* output = lua_newbuffer(args.RawState(), (static_cast<size_t>(units) + 1) * sizeof(wchar_t));
        auto* wide = static_cast<wchar_t*>(output);
        if (units != 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide, units);
        wide[units] = L'\0';
        return 1;
#else
        Error("Address.WBuffer: UTF-16 helpers are only available on Windows");
#endif
    }));
    exports.Set("Pin", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 1) Error("Address.Pin: expected an owner");
        void* pointer = Pointer(args.RawState(), 1, "Address.Pin");
        Lode::State current(args.RawState());
        auto* raw = static_cast<LeaseUserdata*>(current.CreateUserdata(sizeof(LeaseUserdata), DestroyLease));
        new (raw) LeaseUserdata{lodeffi::kMemoryLeaseMagic, args[0].ToValue(), MemoryOwner(args.RawState(), 1), 0, pointer, false};
        if (raw->memoryBlock) raw->generation = raw->memoryBlock->control->generation;
        Lode::Value hidden = Lode::Value::FromLuaState(args.RawState(), -1);
        lua_pop(args.RawState(), 1);
        Lode::Table lease = current.CreateTable();
        lease.Set("Pointer", Lode::Value(pointer));
        lease.Set("__addressLease", hidden);
        lease.Set("Close", current.CreateFastFunctionNNoYield([lease, raw](Lode::State&, Lode::StackArgs closeArgs) mutable -> int { if (closeArgs.Size() != 0) Error("Lease.Close: expected no arguments"); raw->closed = true; raw->owner = Lode::Value(); lease.Set("Pointer", Lode::Value()); return 0; }));
        Lode::Value(lease).PushToLuaState(args.RawState()); return 1;
    }));
    exports.Set("KeepAlive", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 2) Error("Address.KeepAlive: expected owner and pointer");
        void* pointer = Pointer(args.RawState(), 2, "Address.KeepAlive");
        Lode::State current(args.RawState());
        auto* raw = static_cast<LeaseUserdata*>(current.CreateUserdata(sizeof(LeaseUserdata), DestroyLease));
        new (raw) LeaseUserdata{lodeffi::kMemoryLeaseMagic, args[0].ToValue(), MemoryOwner(args.RawState(), 1), 0, pointer, false};
        if (raw->memoryBlock) raw->generation = raw->memoryBlock->control->generation;
        Lode::Value hidden = Lode::Value::FromLuaState(args.RawState(), -1);
        lua_pop(args.RawState(), 1);
        Lode::Table lease = current.CreateTable();
        lease.Set("Pointer", Lode::Value(pointer));
        lease.Set("__addressLease", hidden);
        lease.Set("Close", current.CreateFastFunctionNNoYield([lease, raw](Lode::State&, Lode::StackArgs closeArgs) mutable -> int { if (closeArgs.Size() != 0) Error("Lease.Close: expected no arguments"); raw->closed = true; raw->owner = Lode::Value(); lease.Set("Pointer", Lode::Value()); return 0; }));
        Lode::Value(lease).PushToLuaState(args.RawState()); return 1;
    }));
    return {exports};
}
