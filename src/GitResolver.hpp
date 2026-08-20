// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct GitTagResolutionResult
{
    std::string version;
    std::string tag;
    std::string commit;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !version.empty() && !tag.empty() && !commit.empty();
    }
};

struct GitCheckoutResult
{
    std::filesystem::path packageRoot;
    std::string commit;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !packageRoot.empty() && !commit.empty();
    }
};

// Resolves a v<SemVer> Git tag. An empty requirement selects the highest stable
// tag; an exact or ^/~ requirement selects the highest matching tag.
LODE_API GitTagResolutionResult ResolveGitTag(
    const std::string& repository,
    const std::string& requirement);

// Clones a resolved Git commit into an operation-scoped staging directory,
// records its immutable commit, and removes repository metadata before the
// package is exposed to the installer.
LODE_API GitCheckoutResult CheckoutGitPackage(
    const std::string& repository,
    const std::filesystem::path& stagingDirectory);

LODE_API GitCheckoutResult CheckoutGitPackageAtCommit(
    const std::string& repository,
    const std::string& commit,
    const std::filesystem::path& stagingDirectory);

} // namespace Lode::Package
