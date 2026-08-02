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

template <typename T>
class ObjectWrap
{
public:
    static void Wrap(State& vm, std::shared_ptr<T> instance, const Table& metatable)
    {
        using Holder = std::shared_ptr<T>;
        void* userMemory = vm.CreateUserdata(sizeof(Holder));
        new (userMemory) Holder(instance);

        vm.SetUserdataMetatable(-1, metatable);
    }

    static std::shared_ptr<T> Unwrap(State& vm, int index)
    {
        if (!vm.IsUserdata(index)) return nullptr;

        using Holder = std::shared_ptr<T>;
        auto* holder = static_cast<Holder*>(vm.GetUserdata(index));
        return holder ? *holder : nullptr;
    }

    static std::shared_ptr<T> Unwrap(const Value& val)
    {
        if (val.GetType() != ValueType::Userdata) return nullptr;
        return nullptr;
    }
};

} // namespace Lode
