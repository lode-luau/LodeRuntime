// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageCache.hpp"

#include "PathUtil.hpp"
#include "Sha256.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using json = nlohmann::json;
using Lode::Detail::PathFromUtf8;
using Lode::Detail::PathToUtf8;

void AddError(CacheLayoutResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

void AddError(CacheIdentityResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

std::optional<fs::path> DefaultHomeDirectory()
{
#if defined(_WIN32)
    if (const char* userProfile = std::getenv("USERPROFILE"); userProfile && *userProfile)
        return PathFromUtf8(userProfile);
#else
    if (const char* home = std::getenv("HOME"); home && *home)
        return PathFromUtf8(home);
#endif
    return std::nullopt;
}

std::optional<fs::path> NormalizeHomeDirectory(const fs::path& homeDirectory)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(homeDirectory, ec);
    if (ec || absolute.empty())
        return std::nullopt;

    return absolute.lexically_normal();
}

const char* SourceName(DependencySource source)
{
    switch (source)
    {
        case DependencySource::Root: return "root";
        case DependencySource::StandardLibrary: return "stdlib";
        case DependencySource::Path: return "path";
        case DependencySource::Git: return "git";
    }
    return "unknown";
}

std::optional<std::string> RelativePackageReference(const DependencyGraph& graph,
                                                    const PackageNode& package)
{
    if (package.source != DependencySource::Path)
        return std::string{};

    if (graph.root >= graph.packages.size())
        return std::nullopt;

    std::error_code ec;
    const fs::path graphRoot = fs::weakly_canonical(graph.packages[graph.root].root, ec);
    if (ec)
        return std::nullopt;

    const fs::path relative = fs::relative(package.root, graphRoot, ec);
    if (ec || relative.empty() || relative == "." || relative.is_absolute())
        return std::nullopt;

    return relative.generic_string();
}

bool IsSafePathComponent(const std::string& value)
{
    if (value.empty() || value == "." || value == "..")
        return false;

    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' || character == '_' || character == '.';
    });
}

} // namespace

CacheLayoutResult ResolvePackageCacheLayout(const fs::path& homeDirectory)
{
    CacheLayoutResult result;
    fs::path selectedHome = homeDirectory;
    if (selectedHome.empty())
    {
        const std::optional<fs::path> defaultHome = DefaultHomeDirectory();
        if (!defaultHome)
        {
            AddError(result, "Cannot resolve the user home directory for the Lode package cache.");
            return result;
        }
        selectedHome = *defaultHome;
    }

    const std::optional<fs::path> normalizedHome = NormalizeHomeDirectory(selectedHome);
    if (!normalizedHome)
    {
        AddError(result, "Cannot resolve the user home directory for the Lode package cache: " +
            PathToUtf8(selectedHome));
        return result;
    }

    PackageCacheLayout layout;
    layout.root = *normalizedHome / ".lode";
    layout.archiveDirectory = layout.root / "cache" / "archives" / "sha256";
    layout.stagingDirectory = layout.root / "cache" / "staging";
    layout.globalModulesDirectory = layout.root / "global" / "lode_modules";
    result.layout = std::move(layout);
    return result;
}

CacheIdentityResult ResolvePackageCacheIdentity(const DependencyGraph& graph,
                                               size_t packageIndex,
                                               const PackageCacheLayout& layout)
{
    CacheIdentityResult result;
    if (graph.root >= graph.packages.size())
    {
        AddError(result, "Cannot resolve a package cache identity: the graph root index is invalid.");
        return result;
    }

    if (packageIndex >= graph.packages.size())
    {
        AddError(result, "Cannot resolve a package cache identity: the package index is invalid.");
        return result;
    }

    const PackageNode& package = graph.packages[packageIndex];
    if (package.source == DependencySource::Root)
    {
        AddError(result, "The graph root is not an installable package cache identity.");
        return result;
    }

    if (package.source == DependencySource::Git)
    {
        AddError(result, "Git package '" + package.name +
            "' has no resolved commit and cannot be assigned a cache identity.");
        return result;
    }

    if (!IsSafePathComponent(package.name) || !IsSafePathComponent(package.version))
    {
        AddError(result, "Package cache identity for '" + package.name +
            "' contains a name or version that is unsafe as a path component.");
        return result;
    }

    json document = {
        { "name", package.name },
        { "version", package.version },
        { "source", SourceName(package.source) }
    };

    if (package.source == DependencySource::Path)
    {
        const std::optional<std::string> reference = RelativePackageReference(graph, package);
        if (!reference)
        {
            AddError(result, "Path package '" + package.name +
                "' has no project-relative source reference for its cache identity.");
            return result;
        }
        document["reference"] = *reference;
    }

    const std::string canonicalDocument = document.dump();
    const std::string digest = Lode::Detail::Sha256Hex(canonicalDocument);

    PackageCacheIdentity identity;
    identity.canonicalDocument = canonicalDocument;
    identity.digest = digest;
    identity.installationDirectory = layout.globalModulesDirectory /
        package.name / package.version / digest;
    result.identity = std::move(identity);
    return result;
}

} // namespace Lode::Package
