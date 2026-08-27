// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageArtifact.hpp"

#include "HttpDownloader.hpp"
#include "PackageArchive.hpp"
#include "PackageManifest.hpp"
#include "PathUtil.hpp"
#include "Platform/Platform.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <system_error>

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathToUtf8;

void AddError(PackageArtifactResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool IsSafeComponent(const std::string& value)
{
    return !value.empty() && value != "." && value != ".." &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '-' ||
                character == '_' || character == '.';
        });
}

std::optional<std::string> GitHubSlug(const std::string& repository)
{
    std::string value = repository;
    const std::vector<std::string> prefixes = {
        "github:",
        "https://github.com/",
        "http://github.com/",
        "ssh://git@github.com/",
        "git@github.com:"
    };
    bool recognized = false;
    for (const std::string& prefix : prefixes)
    {
        if (value.rfind(prefix, 0) == 0)
        {
            value.erase(0, prefix.size());
            recognized = true;
            break;
        }
    }
    if (!recognized)
        return std::nullopt;

    while (!value.empty() && value.back() == '/')
        value.pop_back();
    if (value.size() > 4 && value.substr(value.size() - 4) == ".git")
        value.resize(value.size() - 4);

    const size_t slash = value.find('/');
    if (slash == std::string::npos || value.find('/', slash + 1) != std::string::npos)
        return std::nullopt;
    const std::string owner = value.substr(0, slash);
    const std::string name = value.substr(slash + 1);
    if (!IsSafeComponent(owner) || !IsSafeComponent(name))
        return std::nullopt;
    return owner + "/" + name;
}

bool IsSha256(std::string_view value)
{
    return value.size() == 64 &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

std::optional<std::string> ParseChecksum(const fs::path& checksumPath,
                                         const std::string& assetName)
{
    std::ifstream file(checksumPath, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;

    std::string line;
    std::getline(file, line);
    std::istringstream input(line);
    std::string digest;
    std::string filename;
    input >> digest >> filename;
    std::transform(digest.begin(), digest.end(), digest.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (!IsSha256(digest))
        return std::nullopt;
    if (!filename.empty())
    {
        while (!filename.empty() && (filename.front() == '*' || filename.front() == ' '))
            filename.erase(filename.begin());
        if (filename != assetName)
            return std::nullopt;
    }
    return digest;
}

std::optional<std::string> ParseChecksumCatalog(const fs::path& checksumPath,
                                                const std::string& assetName)
{
    std::ifstream file(checksumPath, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream input(line);
        std::string digest;
        std::string filename;
        input >> digest >> filename;
        while (!filename.empty() && (filename.front() == '*' || filename.front() == ' '))
            filename.erase(filename.begin());
        std::transform(digest.begin(), digest.end(), digest.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (filename == assetName && IsSha256(digest))
            return digest;
    }
    return std::nullopt;
}

std::optional<fs::path> FindPackageInBundle(const fs::path& bundleRoot,
                                            const std::string& packageName,
                                            const std::string& packageVersion,
                                            PackageArtifactResult& result)
{
    std::ifstream input(bundleRoot / "index.json", std::ios::binary);
    if (!input.is_open())
    {
        AddError(result, "Stdlib bundle has no index.json catalog.");
        return std::nullopt;
    }

    nlohmann::json index;
    try
    {
        input >> index;
    }
    catch (const std::exception& error)
    {
        AddError(result, "Stdlib bundle index.json is invalid: " + std::string(error.what()));
        return std::nullopt;
    }
    if (!index.is_object() || index.value("format", 0) != 1 ||
        !index.contains("packages") || !index["packages"].is_object() ||
        !index["packages"].contains(packageName) ||
        !index["packages"][packageName].is_object())
    {
        AddError(result, "Stdlib bundle index.json has no entry for package '" + packageName + "'.");
        return std::nullopt;
    }

    const nlohmann::json& entry = index["packages"][packageName];
    const std::string indexedVersion = entry.value("version", "");
    const std::string indexedPath = entry.value("path", "");
    if (indexedVersion != packageVersion || indexedPath.empty())
    {
        AddError(result, "Stdlib bundle index entry for '" + packageName +
            "' does not match the requested version.");
        return std::nullopt;
    }

    const fs::path relativePath = Lode::Detail::PathFromUtf8(indexedPath);
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.root_name() != fs::path() ||
        relativePath.root_directory() != fs::path() ||
        *relativePath.begin() == ".." || relativePath.lexically_normal() != relativePath)
    {
        AddError(result, "Stdlib bundle index path escapes the bundle: " + indexedPath);
        return std::nullopt;
    }

    const fs::path packageRoot = bundleRoot / relativePath;
    const fs::path manifestPath = packageRoot / "package.luau";
    if (!fs::is_regular_file(manifestPath))
    {
        AddError(result, "Stdlib bundle index path has no package.luau: " +
            PathToUtf8(packageRoot));
        return std::nullopt;
    }
    const PackageManifestResult parsed = ReadPackageManifest(manifestPath);
    if (!parsed.IsValid() || parsed.manifest.name != packageName ||
        parsed.manifest.version != packageVersion)
    {
        AddError(result, "Stdlib bundle package manifest does not match its index entry: " +
            PathToUtf8(manifestPath));
        return std::nullopt;
    }
    return packageRoot;
}

bool MoveVerifiedArchive(const fs::path& downloaded,
                         const fs::path& cached,
                         const std::string& expected,
                         PackageArtifactResult& result)
{
    std::error_code ec;
    if (fs::is_regular_file(cached, ec))
    {
        if (Lode::Detail::Sha256FileHex(cached) == expected)
        {
            fs::remove(downloaded, ec);
            return true;
        }
        AddError(result, "Cached package archive has an unexpected SHA-256: " +
            PathToUtf8(cached));
        return false;
    }
    ec.clear();
    if (ec || fs::exists(cached, ec))
    {
        AddError(result, "Cached package archive path is not a regular file: " +
            PathToUtf8(cached));
        return false;
    }

    fs::create_directories(cached.parent_path(), ec);
    if (ec)
    {
        AddError(result, "Cannot create package archive cache: " + ec.message());
        return false;
    }
    fs::rename(downloaded, cached, ec);
    if (ec)
    {
        AddError(result, "Cannot finalize package archive cache: " + ec.message());
        return false;
    }
    return true;
}

} // namespace

PackageArtifactResult DownloadGitHubReleaseArtifact(
    const std::string& repository,
    const std::string& packageName,
    const std::string& packageVersion,
    const std::string& release,
    const std::string& assetName,
    const PackageCacheLayout& cacheLayout,
    const fs::path& stagingDirectory,
    const std::string& packageKind)
{
    PackageArtifactResult result;
    const std::optional<std::string> slug = GitHubSlug(repository);
    if (!slug)
    {
        AddError(result, packageKind + " package '" + packageName +
            "' does not use a supported GitHub repository reference.");
        return result;
    }
    if (!IsSafeComponent(packageName) || !IsSafeComponent(packageVersion) ||
        !IsSafeComponent(release) || !IsSafeComponent(assetName))
    {
        AddError(result, packageKind + " package artifact has an unsafe name, version, release, or asset.");
        return result;
    }

    const std::string platform(Platform::GetOSName());
    const std::string architecture(Platform::GetArchitectureName());
    if (platform == "unknown" || architecture == "unknown")
    {
        AddError(result, "The current runtime target cannot select a native package artifact.");
        return result;
    }

    const std::string baseUrl = "https://github.com/" + *slug +
        "/releases/download/" + release + "/";
    const std::string operationKey = Lode::Detail::Sha256Hex(
        *slug + "\n" + release + "\n" + assetName).substr(0, 16);
    const fs::path operationRoot = stagingDirectory /
        ("artifact-download-" + operationKey);
    const bool isStdlibBundle = packageKind == "stdlib";
    const std::string checksumAsset = isStdlibBundle ? "SHA256SUMS" : assetName + ".sha256";
    const fs::path checksumPath = operationRoot / checksumAsset;
    const fs::path downloadedPath = operationRoot / assetName;

    std::error_code ec;
    fs::remove_all(operationRoot, ec);
    if (!fs::create_directories(operationRoot, ec) || ec)
    {
        AddError(result, "Cannot create artifact download staging: " + ec.message());
        return result;
    }

    const DownloadResult checksum = DownloadHttpsFile(
        baseUrl + checksumAsset, checksumPath);
    if (!checksum.IsValid())
    {
        result.errors.insert(result.errors.end(), checksum.errors.begin(), checksum.errors.end());
        return result;
    }
    const std::optional<std::string> expected = isStdlibBundle
        ? ParseChecksumCatalog(checksumPath, assetName)
        : ParseChecksum(checksumPath, assetName);
    if (!expected)
    {
        AddError(result, "GitHub Release checksum for '" + assetName +
            "' is invalid or names a different asset.");
        return result;
    }

    const fs::path cachedPath = cacheLayout.archiveDirectory / (*expected + ".zip");
    if (!fs::is_regular_file(cachedPath, ec))
    {
        const DownloadResult archive = DownloadHttpsFile(
            baseUrl + assetName, downloadedPath);
        if (!archive.IsValid())
        {
            result.errors.insert(result.errors.end(), archive.errors.begin(), archive.errors.end());
            return result;
        }
        try
        {
            if (Lode::Detail::Sha256FileHex(downloadedPath) != *expected)
            {
                AddError(result, "Downloaded package archive SHA-256 does not match its release checksum.");
                fs::remove(downloadedPath, ec);
                return result;
            }
        }
        catch (const std::exception& error)
        {
            AddError(result, error.what());
            fs::remove(downloadedPath, ec);
            return result;
        }
        if (!MoveVerifiedArchive(downloadedPath, cachedPath, *expected, result))
            return result;
    }
    else
    {
        try
        {
            if (Lode::Detail::Sha256FileHex(cachedPath) != *expected)
            {
                AddError(result, "Cached package archive SHA-256 does not match its lock identity.");
                return result;
            }
        }
        catch (const std::exception& error)
        {
            AddError(result, error.what());
            return result;
        }
    }

    const fs::path extractionRoot = stagingDirectory / ("artifact-" + operationKey);
    const ArchiveExtractionResult extraction = ExtractVerifiedArchive(
        cachedPath, *expected, extractionRoot);
    if (!extraction.IsValid())
    {
        result.errors.insert(result.errors.end(),
            extraction.errors.begin(), extraction.errors.end());
        return result;
    }

    result.packageRoot = extraction.packageRoot;
    if (isStdlibBundle)
    {
        const std::optional<fs::path> packageRoot = FindPackageInBundle(
            extraction.packageRoot, packageName, packageVersion, result);
        if (!packageRoot)
        {
            if (result.IsValid())
                AddError(result, "Stdlib bundle does not contain package '" +
                    packageName + "@" + packageVersion + "'.");
            return result;
        }
        result.packageRoot = *packageRoot;
    }
    result.artifact = {
        platform,
        architecture,
        "",
        LodeAbiId(),
        release,
        assetName,
        *expected
    };
    return result;
}

PackageArtifactResult DownloadGitHubPackageArtifact(
    const std::string& repository,
    const std::string& packageName,
    const std::string& packageVersion,
    const PackageCacheLayout& cacheLayout,
    const fs::path& stagingDirectory)
{
    return DownloadGitHubReleaseArtifact(
        repository,
        packageName,
        packageVersion,
        "v" + packageVersion,
        "lode-" + packageName + "-" + packageVersion + "-" +
            std::string(Platform::GetOSName()) + "-" +
            std::string(Platform::GetArchitectureName()) + ".zip",
        cacheLayout,
        stagingDirectory,
        "native Git");
}

PackageArtifactResult DownloadGitHubStdlibArtifact(
    const std::string& repository,
    const std::string& packageName,
    const std::string& packageVersion,
    const std::string& release,
    const PackageCacheLayout& cacheLayout,
    const fs::path& stagingDirectory)
{
    return DownloadGitHubReleaseArtifact(
        repository,
        packageName,
        packageVersion,
        release,
        "lode-stdlib-" + std::string(Platform::GetOSName()) + "-" +
            std::string(Platform::GetArchitectureName()) + "-" + release + ".zip",
        cacheLayout,
        stagingDirectory,
        "stdlib");
}

} // namespace Lode::Package
