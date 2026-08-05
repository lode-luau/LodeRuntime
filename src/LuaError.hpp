#pragma once

#include "lua.h"
#include <string>

namespace Lode
{

inline std::string LuaErrorMessage(lua_State* L, int index)
{
    if (!L)
        return "Lua error (invalid state)";

    if (const char* message = lua_tostring(L, index))
        return message;

    const char* typeName = lua_typename(L, lua_type(L, index));
    return std::string("Lua error (value of type ") + (typeName ? typeName : "unknown") + ")";
}

} // namespace Lode
