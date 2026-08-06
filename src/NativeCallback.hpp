#pragma once

#include "lua.h"
#include <new>
#include <utility>

namespace Lode::Detail
{

template <typename T>
T* NewLuaOwnedCallbackData(lua_State* L, T value)
{
    auto* data = static_cast<T*>(lua_newuserdatadtor(L, sizeof(T), [](void* ptr) {
        static_cast<T*>(ptr)->~T();
    }));
    new (data) T(std::move(value));
    return data;
}

} // namespace Lode::Detail
