// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lode::Package
{

struct ArchiveExtractionResult
{
    std::filesystem::path packageRoot;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !packageRoot.empty();
    }
};

// Verifies an archive against its required SHA-256 and extracts it into a
// fresh operation-scoped directory. ZIP entries must remain below the
// extraction root; failed extractions remove the destination completely.
LODE_API ArchiveExtractionResult ExtractVerifiedArchive(
    const std::filesystem::path& archivePath,
    std::string_view expectedSha256,
    const std::filesystem::path& extractionRoot);

} // namespace Lode::Package
