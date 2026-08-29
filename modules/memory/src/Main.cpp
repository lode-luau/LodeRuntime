// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/StackValue.hpp"
#include "lua.h"

#include "NativeMemory.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
using lodeffi::MemoryBlockUserdata;
using lodeffi::MemoryControl;

[[noreturn]] void Error(const char* message) { throw std::runtime_error(message); }

size_t SizeArgument(const Lode::StackValue& value, const char* name)
{
    if (!value.IsNumber()) throw std::runtime_error(std::string("Memory.") + name + ": expected a non-negative integer");
    const double number = value.AsNumber();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<size_t>::max()))
        throw std::runtime_error(std::string("Memory.") + name + ": expected a non-negative integer");
    return static_cast<size_t>(number);
}

MemoryBlockUserdata* Block(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA) Error("Memory: expected a MemoryBlock");
    auto* block = static_cast<MemoryBlockUserdata*>(lua_touserdata(L, index));
    if (block == nullptr || block->magic != lodeffi::kMemoryBlockMagic || !block->control)
        Error("Memory: expected a MemoryBlock");
    return block;
}

MemoryControl& LiveBlock(lua_State* L, int index)
{
    MemoryControl& control = *Block(L, index)->control;
    if (!control.alive) Error("Memory: block is no longer alive");
    return control;
}

void DestroyBlock(void* raw)
{
    static_cast<MemoryBlockUserdata*>(raw)->~MemoryBlockUserdata();
}

void PushNewBlock(lua_State* L, std::shared_ptr<MemoryControl> control)
{
    auto* raw = static_cast<MemoryBlockUserdata*>(lua_newuserdatadtor(L, sizeof(MemoryBlockUserdata), DestroyBlock));
    new (raw) MemoryBlockUserdata{lodeffi::kMemoryBlockMagic, std::move(control)};
}
} // namespace

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    exports.Set("new", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() < 1 || args.Size() > 2) Error("Memory.new: expected byteCount and optional options");
        const size_t size = SizeArgument(args[0], "new");
        bool zeroed = false;
        size_t alignment = alignof(std::max_align_t);
        if (args.Size() == 2 && !args[1].IsNil())
        {
            if (!args[1].IsTable()) Error("Memory.new: options must be a table");
            const Lode::Table options = args[1].AsTable();
            for (const std::string& key : options.GetKeys())
                if (key != "Zeroed" && key != "Alignment") Error("Memory.new: unknown option");
            const auto zero = options.Get("Zeroed");
            if (zero.IsOk() && !zero.GetValue().IsNil())
            {
                if (!zero.GetValue().IsBoolean()) Error("Memory.new: Zeroed must be a boolean");
                zeroed = zero.GetValue().AsBoolean();
            }
            const auto align = options.Get("Alignment");
            if (align.IsOk() && !align.GetValue().IsNil())
            {
                if (!align.GetValue().IsNumber()) Error("Memory.new: Alignment must be a number");
                const double requested = align.GetValue().AsNumber();
                if (!std::isfinite(requested) || requested < 0 || std::floor(requested) != requested ||
                    requested > static_cast<double>(std::numeric_limits<size_t>::max()))
                    Error("Memory.new: Alignment must be a power of two");
                if (requested != 0) alignment = static_cast<size_t>(requested);
            }
        }
        if (!lodeffi::IsPowerOfTwo(alignment) || alignment < alignof(void*))
            Error("Memory.new: Alignment must be a power of two at least pointer alignment");
        auto control = std::make_shared<MemoryControl>();
        control->alignment = alignment;
        control->size = size;
        control->alive = true;
        if (size != 0)
        {
            control->data = ::operator new(size, std::align_val_t(alignment));
            if (zeroed) std::memset(control->data, 0, size);
        }
        PushNewBlock(args.RawState(), std::move(control));
        return 1;
    }));
    exports.Set("Size", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 1) Error("Memory.Size: expected a MemoryBlock");
        lua_pushnumber(args.RawState(), static_cast<double>(Block(args.RawState(), 1)->control->size));
        return 1;
    }));
    exports.Set("IsAlive", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 1) Error("Memory.IsAlive: expected a MemoryBlock");
        lua_pushboolean(args.RawState(), Block(args.RawState(), 1)->control->alive);
        return 1;
    }));
    exports.Set("Address", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() < 1 || args.Size() > 2) Error("Memory.Address: expected block and optional offset");
        MemoryControl& control = LiveBlock(args.RawState(), 1);
        const size_t offset = args.Size() == 2 ? SizeArgument(args[1], "Address") : 0;
        if (offset > control.size) Error("Memory.Address: offset exceeds block size");
        if (control.data == nullptr) { lua_pushnil(args.RawState()); return 1; }
        lua_pushlightuserdata(args.RawState(), static_cast<unsigned char*>(control.data) + offset);
        return 1;
    }));
    exports.Set("Resize", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 2) Error("Memory.Resize: expected block and newByteCount");
        MemoryControl& control = LiveBlock(args.RawState(), 1);
        const size_t nextSize = SizeArgument(args[1], "Resize");
        void* next = nextSize == 0 ? nullptr : ::operator new(nextSize, std::align_val_t(control.alignment));
        if (next != nullptr && control.data != nullptr) std::memcpy(next, control.data, std::min(control.size, nextSize));
        if (control.data != nullptr) ::operator delete(control.data, std::align_val_t(control.alignment));
        control.data = next;
        control.size = nextSize;
        ++control.generation;
        return 0;
    }));
    exports.Set("Zero", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() < 1 || args.Size() > 3) Error("Memory.Zero: expected block and optional offset and size");
        MemoryControl& control = LiveBlock(args.RawState(), 1);
        const size_t offset = args.Size() >= 2 ? SizeArgument(args[1], "Zero") : 0;
        if (offset > control.size) Error("Memory.Zero: range exceeds block size");
        const size_t count = args.Size() == 3 ? SizeArgument(args[2], "Zero") : control.size - offset;
        if (count > control.size - offset) Error("Memory.Zero: range exceeds block size");
        if (count != 0) std::memset(static_cast<unsigned char*>(control.data) + offset, 0, count);
        return 0;
    }));
    exports.Set("Free", vm.CreateFastFunctionNNoYield([](Lode::State&, Lode::StackArgs args) -> int {
        if (args.Size() != 1) Error("Memory.Free: expected a MemoryBlock");
        Block(args.RawState(), 1)->control->Free();
        return 0;
    }));
    return {exports};
}
