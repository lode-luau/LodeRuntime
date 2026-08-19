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

// Installs an already locked source graph using the installed stdlib catalog
// and local path dependencies. This first implementation does not resolve Git,
// download archives, or build native packages.
LODE_API InstallResult InstallLocked(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

// Resolves and installs the currently available stdlib and local path graph,
// then writes its deterministic lockfile. Git resolution, downloads, and
// native package builds are intentionally unsupported by this local resolver.
LODE_API InstallResult InstallLocal(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

} // namespace Lode::Package
