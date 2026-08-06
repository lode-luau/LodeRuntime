#pragma once

#include "lua.h"
#include "lualib.h"
#include <exception>
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

inline int RaiseCppException(lua_State* L, const char* context, const std::exception& error)
{
    luaL_error(L, "%s: %s", context, error.what());
    return 0;
}

inline int RaiseCppException(lua_State* L, const char* context)
{
    luaL_error(L, "%s", context);
    return 0;
}

} // namespace Lode
