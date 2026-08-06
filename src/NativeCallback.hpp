#pragma once

#include "lua.h"
#include <new>
#include <utility>

namespace Lode::Detail
{

template <typename T>
T* NewLuaOwnedCallbackData(lua_State* L, T value)
{
    auto* data = static_cast<T*>(lua_newuserdata(L, sizeof(T)));
    new (data) T(std::move(value));

    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* state) -> int {
        auto* owned = static_cast<T*>(lua_touserdata(state, 1));
        if (owned)
            owned->~T();
        return 0;
    }, "__gc");
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    return data;
}

} // namespace Lode::Detail
