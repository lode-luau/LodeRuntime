// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PackageValidator.hpp"

#include <filesystem>
#include <string>

namespace Lode::Package
{

struct CiSdkPin
{
    std::string version;
    std::string sha256;

    bool IsValid() const;
};

LODE_API ValidationReport GenerateWorkflow(const std::filesystem::path& packageRoot,
                                           bool force,
                                           const CiSdkPin& sdkPin,
                                           const std::filesystem::path& standardLibraryRoot = {});
LODE_API ValidationReport UpdateWorkflow(const std::filesystem::path& packageRoot,
                                         const std::filesystem::path& standardLibraryRoot = {});

} // namespace Lode::Package
