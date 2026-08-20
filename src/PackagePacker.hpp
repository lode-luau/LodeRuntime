// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct PackResult
{
    std::vector<std::string> errors;
    std::filesystem::path archivePath;
    std::filesystem::path checksumPath;

    bool IsValid() const
    {
        return errors.empty() && !archivePath.empty() && !checksumPath.empty();
    }
};

// Validates and publishes the current package as the supported Windows x64
// ZIP artifact. The output receives a sibling .sha256 checksum file.
LODE_API PackResult PackPackage(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& standardLibraryRoot,
    const std::filesystem::path& outputArchive = {});

} // namespace Lode::Package
