// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include <memory>
#include <typeinfo>
#include <string>

namespace Lode
{

/**
 * @brief Helper template for binding C++ objects to Luau Userdata.
 * 
 * Provides static methods to safely wrap a std::shared_ptr<T> into a Luau Userdata
 * and retrieve it back. It ensures the C++ object lifetime is bound to the Lua GC.
 */
template <typename T>
class ObjectWrap
{
public:
    /**
     * @brief Allocates a Luau userdata and places a copy of the shared_ptr inside it.
     * @param vm The state to execute in.
     * @param instance The C++ object to wrap.
     * @param metatable The metatable to assign to the Userdata.
     */
    static void Wrap(State& vm, std::shared_ptr<T> instance, const Table& metatable)
    {
        using Holder = std::shared_ptr<T>;
        void* userMemory = vm.CreateUserdata(sizeof(Holder), [](void* ptr) {
            static_cast<Holder*>(ptr)->~Holder();
        });
        new (userMemory) Holder(instance);

        vm.SetUserdataMetatable(-1, metatable);
    }

    /**
     * @brief Retrieves a shared_ptr from a userdata at the given stack index.
     * @param vm The state to execute in.
     * @param index The stack index.
     * @return The unwrapped shared_ptr, or nullptr if it's not a valid userdata.
     */
    static std::shared_ptr<T> Unwrap(State& vm, int index)
    {
        if (!vm.IsUserdata(index)) return nullptr;

        using Holder = std::shared_ptr<T>;
        auto* holder = static_cast<Holder*>(vm.GetUserdata(index));
        return holder ? *holder : nullptr;
    }

    /**
     * @brief Attempts to extract a shared_ptr from a Value representing Userdata.
     * @param val The Value to unwrap.
     * @return The unwrapped shared_ptr, or nullptr.
     * A Value does not expose its captured state, so userdata can only be
     * unwrapped through a live State (see the stack-index overload).
     */
    static std::shared_ptr<T> Unwrap(const Value& val)
    {
        (void)val;
        return nullptr;
    }
};

} // namespace Lode
