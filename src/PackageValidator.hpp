// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "PackageGraph.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

enum class ValidationMode
{
    Source,
    Artifact,
};

struct ValidationReport
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    DependencyGraph dependencyGraph;

    bool IsValid() const
    {
        return errors.empty();
    }
};

// Checks a resolved package version against the exact or ^/~ requirement
// syntax accepted by lode.json dependency declarations.
LODE_API bool PackageVersionSatisfies(const std::string& actual,
                                      const std::string& requirement);

LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot);
LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot, ValidationMode mode);
LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot,
                                   ValidationMode mode,
                                   const std::filesystem::path& standardLibraryRoot);

} // namespace Lode::Package
