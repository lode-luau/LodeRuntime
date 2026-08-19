// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageCache.hpp"

#include "PathUtil.hpp"
#include "Sha256.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>
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

void AddError(MaterializationResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

void AddError(CachePopulationResult& result, std::string message)
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

bool IsPathInside(const fs::path& candidate, const fs::path& root)
{
    std::error_code ec;
    const fs::path relative = fs::relative(candidate, root, ec);
    if (ec)
        return false;
    if (relative.empty() || relative == ".")
        return true;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

bool ContainsSymlink(const fs::path& root)
{
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
         it != end && !ec; it.increment(ec))
    {
        if (it->is_symlink(ec))
            return true;
    }
    return ec.value() != 0;
}

bool CopyDirectoryContents(const fs::path& source,
                           const fs::path& destination,
                           std::error_code& error)
{
    for (fs::directory_iterator it(source, error), end; it != end && !error; it.increment(error))
    {
        fs::copy(it->path(), destination / it->path().filename(),
            fs::copy_options::recursive, error);
    }
    return !error;
}

bool RenameWithRetry(const fs::path& source,
                     const fs::path& destination,
                     std::error_code& error)
{
    constexpr int attempts = 20;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        error.clear();
        fs::rename(source, destination, error);
        if (!error)
            return true;

        if (attempt + 1 < attempts)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool FilesEqual(const fs::path& left, const fs::path& right, std::error_code& error)
{
    const auto leftSize = fs::file_size(left, error);
    if (error)
        return false;
    const auto rightSize = fs::file_size(right, error);
    if (error || leftSize != rightSize)
        return false;

    std::ifstream leftFile(left, std::ios::binary);
    std::ifstream rightFile(right, std::ios::binary);
    if (!leftFile.is_open() || !rightFile.is_open())
    {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }

    std::array<char, 64 * 1024> leftBuffer{};
    std::array<char, 64 * 1024> rightBuffer{};
    while (leftFile && rightFile)
    {
        leftFile.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        rightFile.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        if (leftFile.gcount() != rightFile.gcount())
            return false;
        if (!std::equal(leftBuffer.begin(),
                        leftBuffer.begin() + leftFile.gcount(),
                        rightBuffer.begin()))
            return false;
    }
    return leftFile.eof() && rightFile.eof();
}

bool DirectoriesEqual(const fs::path& left, const fs::path& right, std::error_code& error)
{
    std::vector<std::string> leftNames;
    std::vector<std::string> rightNames;
    for (fs::directory_iterator it(left, error), end; it != end && !error; it.increment(error))
        leftNames.push_back(it->path().filename().generic_string());
    if (error)
        return false;
    for (fs::directory_iterator it(right, error), end; it != end && !error; it.increment(error))
        rightNames.push_back(it->path().filename().generic_string());
    if (error)
        return false;

    std::sort(leftNames.begin(), leftNames.end());
    std::sort(rightNames.begin(), rightNames.end());
    if (leftNames != rightNames)
        return false;

    for (const std::string& name : leftNames)
    {
        const fs::path leftEntry = left / name;
        const fs::path rightEntry = right / name;
        const bool leftDirectory = fs::is_directory(leftEntry, error);
        const bool rightDirectory = fs::is_directory(rightEntry, error);
        if (error || leftDirectory != rightDirectory)
            return false;
        if (leftDirectory)
        {
            if (!DirectoriesEqual(leftEntry, rightEntry, error))
                return false;
        }
        else if (!FilesEqual(leftEntry, rightEntry, error))
            return false;
    }
    return !error;
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

CachePopulationResult PopulatePackageCache(const PackageCacheLayout& layout,
                                           const PackageCacheIdentity& identity,
                                           const fs::path& sourceRoot)
{
    CachePopulationResult result;
    result.installationDirectory = identity.installationDirectory;

    std::error_code ec;
    const fs::path globalRoot = fs::weakly_canonical(layout.globalModulesDirectory, ec);
    const fs::path destination = fs::weakly_canonical(identity.installationDirectory, ec);
    if (ec || !IsPathInside(destination, globalRoot))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot populate package cache: identity directory is outside the Lode cache.");
        return result;
    }

    const fs::path source = fs::weakly_canonical(sourceRoot, ec);
    if (ec || !fs::is_directory(source, ec))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot populate package cache: source package is not a directory: " +
            PathToUtf8(sourceRoot));
        return result;
    }
    if (ContainsSymlink(source))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot populate package cache: source package contains a symbolic link.");
        return result;
    }

    if (fs::is_symlink(destination, ec))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot populate package cache: identity directory is a symbolic link.");
        return result;
    }
    if (fs::is_directory(destination, ec))
    {
        if (ContainsSymlink(destination) || !DirectoriesEqual(source, destination, ec))
        {
            result.installationDirectory.clear();
            AddError(result, "Cannot populate package cache: existing identity differs from the source package.");
            return result;
        }
        result.reused = true;
        return result;
    }
    if (fs::exists(destination, ec))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot populate package cache: identity path is not a directory.");
        return result;
    }

    const fs::path parent = destination.parent_path();
    if (!fs::create_directories(parent, ec) && ec)
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot create package cache directory: " + ec.message());
        return result;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temporary = parent /
        ("." + destination.filename().string() + ".tmp-" + std::to_string(timestamp));
    if (!fs::create_directory(temporary, ec))
    {
        result.installationDirectory.clear();
        AddError(result, "Cannot create temporary package cache directory: " + ec.message());
        return result;
    }

    if (!CopyDirectoryContents(source, temporary, ec))
    {
        const std::string message = "Cannot populate package cache: " + ec.message();
        fs::remove_all(temporary, ec);
        result.installationDirectory.clear();
        AddError(result, message);
        return result;
    }

    RenameWithRetry(temporary, destination, ec);
    if (ec)
    {
        std::error_code destinationError;
        if (fs::is_directory(destination, destinationError) &&
            !ContainsSymlink(destination) &&
            DirectoriesEqual(source, destination, destinationError))
        {
            fs::remove_all(temporary, destinationError);
            result.reused = true;
            return result;
        }
        const std::string message = "Cannot finalize package cache installation from '" +
            PathToUtf8(temporary) + "' to '" + PathToUtf8(destination) + "': " + ec.message();
        fs::remove_all(temporary, ec);
        result.installationDirectory.clear();
        AddError(result, message);
    }
    return result;
}

MaterializationResult MaterializePackage(const PackageCacheLayout& layout,
                                         const PackageCacheIdentity& identity,
                                         const fs::path& projectRoot,
                                         std::string_view alias)
{
    MaterializationResult result;
    if (!IsSafePathComponent(std::string(alias)))
    {
        AddError(result, "Cannot materialize package: unsafe dependency alias '" +
            std::string(alias) + "'.");
        return result;
    }

    std::error_code ec;
    const fs::path canonicalProject = fs::weakly_canonical(projectRoot, ec);
    if (ec || !fs::is_directory(canonicalProject, ec))
    {
        AddError(result, "Cannot materialize package: project root is not a directory: " +
            PathToUtf8(projectRoot));
        return result;
    }

    const fs::path canonicalGlobalRoot = fs::weakly_canonical(layout.globalModulesDirectory, ec);
    const fs::path canonicalSource = fs::weakly_canonical(identity.installationDirectory, ec);
    if (ec || !fs::is_directory(canonicalSource, ec) ||
        !IsPathInside(canonicalSource, canonicalGlobalRoot))
    {
        AddError(result, "Cannot materialize package: the global installation is missing or outside the Lode cache.");
        return result;
    }
    if (ContainsSymlink(canonicalSource))
    {
        AddError(result, "Cannot materialize package: the global installation contains a symbolic link.");
        return result;
    }

    const fs::path modulesDirectory = canonicalProject / "lode_modules";
    const fs::path destination = modulesDirectory / std::string(alias);
    if (!IsPathInside(destination, modulesDirectory))
    {
        AddError(result, "Cannot materialize package: resolved destination leaves lode_modules.");
        return result;
    }
    if (fs::exists(destination, ec) || fs::is_symlink(destination, ec))
    {
        if (fs::is_directory(destination, ec) && !ContainsSymlink(destination) &&
            DirectoriesEqual(canonicalSource, destination, ec))
        {
            result.packageDirectory = destination;
            result.reused = true;
            return result;
        }
        AddError(result, "Cannot materialize package: destination already exists: " +
            PathToUtf8(destination));
        return result;
    }

    if (!fs::create_directories(modulesDirectory, ec) && ec)
    {
        AddError(result, "Cannot create lode_modules: " + ec.message());
        return result;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temporary = modulesDirectory /
        ("." + std::string(alias) + ".tmp-" + std::to_string(timestamp));
    fs::create_directory(temporary, ec);
    if (ec)
    {
        AddError(result, "Cannot create temporary package directory: " + ec.message());
        return result;
    }

    if (!CopyDirectoryContents(canonicalSource, temporary, ec))
    {
        const std::string message = "Cannot copy global package installation: " + ec.message();
        fs::remove_all(temporary, ec);
        AddError(result, message);
        return result;
    }

    RenameWithRetry(temporary, destination, ec);
    if (ec)
    {
        std::error_code destinationError;
        if (fs::is_directory(destination, destinationError) &&
            !ContainsSymlink(destination) &&
            DirectoriesEqual(canonicalSource, destination, destinationError))
        {
            fs::remove_all(temporary, destinationError);
            result.packageDirectory = destination;
            result.reused = true;
            return result;
        }
        const std::string message = "Cannot finalize local package installation from '" +
            PathToUtf8(temporary) + "' to '" + PathToUtf8(destination) + "': " + ec.message();
        fs::remove_all(temporary, ec);
        AddError(result, message);
        return result;
    }

    result.packageDirectory = destination;
    return result;
}

} // namespace Lode::Package
