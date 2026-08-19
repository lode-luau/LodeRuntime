// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct ValidationReport
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool IsValid() const
    {
        return errors.empty();
    }
};

LODE_API ValidationReport Validate(const std::filesystem::path& packageRoot);

} // namespace Lode::Package
