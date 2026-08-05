#pragma once

#include "lua.h"
#include <string>

namespace Lode
{

inline std::string LuaErrorMessage(lua_State* L, int index)
{
    if (!L)
        return "Lua error (invalid state)";

    size_t length = 0;
    if (const char* message = lua_tolstring(L, index, &length))
        return std::string(message, length);

    const char* typeName = lua_typename(L, lua_type(L, index));
    return std::string("Lua error (value of type ") + (typeName ? typeName : "unknown") + ")";
}

} // namespace Lode
