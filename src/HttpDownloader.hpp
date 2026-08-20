// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct DownloadResult
{
    std::filesystem::path destination;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !destination.empty();
    }
};

// Downloads one HTTPS release asset to an explicit temporary destination.
// The package manager supplies URLs derived from its GitHub Release contract;
// this function is not a general-purpose URL or redirect launcher.
LODE_API DownloadResult DownloadHttpsFile(
    const std::string& url,
    const std::filesystem::path& destination);

} // namespace Lode::Package
