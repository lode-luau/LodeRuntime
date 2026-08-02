#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "lua.h"
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
        lua_State* L = vm.GetLuaState();
        if (!L) return;

        using Holder = std::shared_ptr<T>;
        void* userMemory = lua_newuserdata(L, sizeof(Holder));
        new (userMemory) Holder(instance);

        metatable.PushToLuaState(L);
        lua_setmetatable(L, -2);
    }

    static std::shared_ptr<T> Unwrap(State& vm, int index)
    {
        lua_State* L = vm.GetLuaState();
        if (!L || !lua_isuserdata(L, index)) return nullptr;

        using Holder = std::shared_ptr<T>;
        auto* holder = static_cast<Holder*>(lua_touserdata(L, index));
        return holder ? *holder : nullptr;
    }

    static std::shared_ptr<T> Unwrap(const Value& val)
    {
        if (val.GetType() != ValueType::Userdata) return nullptr;
        // Val stores userdata via reference or lua_State
        return nullptr;
    }
};

} // namespace Lode
