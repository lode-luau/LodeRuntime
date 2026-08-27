// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "PackageValidator.hpp"

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

// Resolves and validates the exact locked graph without materializing package
// directories or updating .config.luau. Locked GitHub artifacts may be
// downloaded into the global cache when they are not cached yet.
LODE_API ValidationReport ValidateLockedPackage(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies,
    ValidationMode mode = ValidationMode::InstallSource);

// Installs an already locked graph using the installed stdlib catalog, local
// path packages, and pinned Git source/release artifacts for the current
// runtime target. It never compiles native packages locally.
LODE_API InstallResult InstallLocked(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

// Resolves and installs stdlib, local path, pure-Luau Git, and supported
// current-platform GitHub Release artifact packages, then writes its
// deterministic lockfile. It never builds native packages locally.
LODE_API InstallResult InstallLocal(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    bool includeDevelopmentDependencies);

} // namespace Lode::Package
