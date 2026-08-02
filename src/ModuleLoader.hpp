#pragma once

#include "Lode/State.hpp"
#include "Registry.hpp"
#include <string>

struct lua_State;

namespace Lode
{

void SetupModuleLoader(lua_State* L, NativeModuleRegistry* registry, const std::vector<std::string>& modulePaths);

} // namespace Lode
