// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "PackageGraph.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Lode::Package
{

struct PackageCacheLayout
{
    std::filesystem::path root;
    std::filesystem::path archiveDirectory;
    std::filesystem::path stagingDirectory;
    std::filesystem::path globalModulesDirectory;
};

struct CacheLayoutResult
{
    std::optional<PackageCacheLayout> layout;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && layout.has_value();
    }
};

struct PackageCacheIdentity
{
    std::string canonicalDocument;
    std::string digest;
    std::filesystem::path installationDirectory;
};

struct CacheIdentityResult
{
    std::optional<PackageCacheIdentity> identity;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && identity.has_value();
    }
};

struct MaterializationResult
{
    std::filesystem::path packageDirectory;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !packageDirectory.empty();
    }
};

// Resolves the decided user-level cache layout without creating any files or
// directories. An explicit home directory is intended for deterministic
// callers and tests; the default uses USERPROFILE on Windows and HOME on
// POSIX systems.
LODE_API CacheLayoutResult ResolvePackageCacheLayout(
    const std::filesystem::path& homeDirectory = {});

// Builds the deterministic identity used below
// <globalModulesDirectory>/<name>/<version>/<digest>. The dependency alias is
// deliberately absent. Root and unresolved Git nodes are not installable
// cache identities in the current graph contract.
LODE_API CacheIdentityResult ResolvePackageCacheIdentity(
    const DependencyGraph& graph,
    size_t packageIndex,
    const PackageCacheLayout& layout);

// Copies an immutable global installation into the project-local
// lode_modules/<alias> view. The destination must not already exist; failed
// copies are removed and never leave a partial package directory behind.
LODE_API MaterializationResult MaterializePackage(
    const PackageCacheLayout& layout,
    const PackageCacheIdentity& identity,
    const std::filesystem::path& projectRoot,
    std::string_view alias);

} // namespace Lode::Package
