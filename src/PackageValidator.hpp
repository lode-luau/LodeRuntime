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

LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot);
LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot, ValidationMode mode);

} // namespace Lode::Package
