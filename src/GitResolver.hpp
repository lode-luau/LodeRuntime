// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

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

// Clones the repository's default revision into an operation-scoped staging
// directory, records its immutable commit, and removes repository metadata
// before the package is exposed to the installer. This does not select GitHub
// Release artifacts or build native packages.
LODE_API GitCheckoutResult CheckoutGitPackage(
    const std::string& repository,
    const std::filesystem::path& stagingDirectory);

LODE_API GitCheckoutResult CheckoutGitPackageAtCommit(
    const std::string& repository,
    const std::string& commit,
    const std::filesystem::path& stagingDirectory);

} // namespace Lode::Package
