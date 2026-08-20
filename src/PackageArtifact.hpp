// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "PackageCache.hpp"
#include "PackageGraph.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct PackageArtifactResult
{
    std::filesystem::path packageRoot;
    PackageArtifact artifact;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !packageRoot.empty() &&
            !artifact.release.empty() && !artifact.asset.empty() &&
            !artifact.sha256.empty();
    }
};

// Resolves the package's v<version> GitHub Release, downloads the exact
// lode-<package>-<version>-windows-x64.zip asset and its checksum, caches the
// verified archive, and extracts it into operation-scoped staging.
LODE_API PackageArtifactResult DownloadGitHubPackageArtifact(
    const std::string& repository,
    const std::string& packageName,
    const std::string& packageVersion,
    const PackageCacheLayout& cacheLayout,
    const std::filesystem::path& stagingDirectory);

} // namespace Lode::Package
