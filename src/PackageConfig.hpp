// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lode::Package
{

struct ConfigAliasUpdateResult
{
    std::string content;
    std::vector<std::string> errors;
    bool changed = false;

    bool IsValid() const
    {
        return errors.empty();
    }
};

// Adds project-relative package aliases to the existing luau.aliases table.
// The source is treated as Luau text; unrelated configuration fields and
// formatting are preserved. Existing aliases are never overwritten.
LODE_API ConfigAliasUpdateResult UpdateConfigAliases(
    std::string_view configContent,
    const std::vector<std::pair<std::string, std::string>>& aliases);

// Applies UpdateConfigAliases and replaces the configuration atomically. The
// file is not changed when parsing or writing fails.
LODE_API ConfigAliasUpdateResult WriteConfigAliases(
    const std::filesystem::path& configPath,
    const std::vector<std::pair<std::string, std::string>>& aliases);

// Creates the minimal Luau configuration used when a project has no existing
// .config.luau file.
LODE_API ConfigAliasUpdateResult WriteGeneratedConfigAliases(
    const std::filesystem::path& configPath,
    const std::vector<std::pair<std::string, std::string>>& aliases);

} // namespace Lode::Package
