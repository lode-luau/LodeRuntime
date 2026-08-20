// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct InstallResult
{
    std::vector<std::string> errors;
    std::vector<std::filesystem::path> materializedPackages;

    bool IsValid() const
    {
        return errors.empty();
    }
};

// Installs an already locked graph using the installed stdlib catalog, local
// path packages, and pinned Git source/release artifacts for the current
// runtime target. It never compiles native packages locally.
LODE_API InstallResult InstallLocked(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

// Resolves and installs stdlib, local path, pure-Luau Git, and supported
// Windows x64 GitHub Release artifact packages, then writes its deterministic
// lockfile. It never builds native packages locally.
LODE_API InstallResult InstallLocal(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

} // namespace Lode::Package
