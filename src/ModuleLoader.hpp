// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/State.hpp"
#include "Registry.hpp"
#include <string>

struct lua_State;

#include <filesystem>

namespace Lode
{

LODE_API void SetupModuleLoader(lua_State* L, NativeModuleRegistry* registry, const std::vector<std::string>& modulePaths);

LODE_API void UpdateModulePaths(lua_State* L, const std::vector<std::string>& modulePaths);

LODE_API std::string GetCallerChunkName(lua_State* L);
LODE_API std::filesystem::path FindLodeJson(const std::filesystem::path& startPath);

} // namespace Lode
