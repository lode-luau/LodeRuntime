// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct ProjectInitOptions
{
    std::string name;
    std::string description;
    std::string version = "0.1.0";
    std::string license = "MIT";
    bool native = false;
};

struct ProjectInitResult
{
    std::vector<std::string> errors;

    [[nodiscard]] bool IsValid() const { return errors.empty(); }
};

LODE_API ProjectInitResult InitializeProject(const std::filesystem::path& projectRoot,
                                             const ProjectInitOptions& options);

} // namespace Lode::Package
