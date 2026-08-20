// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackagePacker.hpp"

#include "PackageArchive.hpp"
#include "PackageInstaller.hpp"
#include "PackageLockfile.hpp"
#include "PackageValidator.hpp"
#include "PathUtil.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <miniz.h>
#include <system_error>

#include "nlohmann/json.hpp"

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using json = nlohmann::json;
using Lode::Detail::PathToUtf8;

void AddError(PackResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool IsUnderTests(const std::string& relativePath)
{
    const std::string normalized = Lowercase(relativePath);
    return normalized == "tests" || normalized.starts_with("tests/");
}

bool IsRuntimeLibrary(const fs::path& relativePath)
{
    const std::string extension = Lowercase(relativePath.extension().string());
    return extension == ".dll" || extension == ".so" || extension == ".dylib";
}

bool IsLuauSource(const fs::path& relativePath)
{
    const std::string extension = Lowercase(relativePath.extension().string());
    return extension == ".lua" || extension == ".luau";
}

bool IsLibraryPath(const std::string& relativePath)
{
    const std::string normalized = Lowercase(relativePath);
    return normalized == "libs" || normalized.starts_with("libs/");
}

bool IsSafeRelativePath(const fs::path& relativePath)
{
    return !relativePath.empty() && !relativePath.is_absolute() &&
        relativePath.root_name().empty() && relativePath.root_directory().empty() &&
        *relativePath.begin() != "..";
}

bool IsSafeArchiveComponent(const std::string& value)
{
    return !value.empty() && value != "." && value != ".." &&
        value.find_first_of("/\\:") == std::string::npos &&
        std::none_of(value.begin(), value.end(), [](unsigned char character) {
            return std::iscntrl(character) != 0;
        });
}

bool IsRegularFileWithoutSymlink(const fs::path& path)
{
    std::error_code ec;
    return !fs::is_symlink(path, ec) && fs::is_regular_file(path, ec);
}

bool ReadManifest(const fs::path& packageRoot, json& manifest, PackResult& result)
{
    std::ifstream file(packageRoot / "lode.json");
    if (!file.is_open())
    {
        AddError(result, "Cannot open package manifest: " +
            PathToUtf8(packageRoot / "lode.json"));
        return false;
    }

    try
    {
        file >> manifest;
    }
    catch (const std::exception& error)
    {
        AddError(result, "Failed to parse package manifest: " + std::string(error.what()));
        return false;
    }
    if (!manifest.is_object())
    {
        AddError(result, "Package manifest must contain a JSON object.");
        return false;
    }
    return true;
}

struct PackageFile
{
    fs::path source;
    std::string archiveName;
};

bool CollectPackageFiles(const fs::path& packageRoot,
                         std::vector<PackageFile>& files,
                         PackResult& result)
{
    std::map<std::string, fs::path> collected;
    auto addFile = [&](const fs::path& relativePath) {
        const fs::path normalized = relativePath.lexically_normal();
        if (!IsSafeRelativePath(normalized))
        {
            AddError(result, "Package file path is unsafe: " + PathToUtf8(relativePath));
            return;
        }

        const fs::path source = packageRoot / normalized;
        if (!IsRegularFileWithoutSymlink(source))
        {
            AddError(result, "Required package file is missing or is a symbolic link: " +
                PathToUtf8(normalized));
            return;
        }

        const std::string archiveName = normalized.generic_string();
        collected.emplace(archiveName, source);
    };

    for (const char* required : { "lode.json", "init.luau", "LICENSE" })
        addFile(required);

    for (const char* optional : { "README.md", "NOTICE" })
    {
        if (IsRegularFileWithoutSymlink(packageRoot / optional))
            addFile(optional);
    }

    std::error_code ec;
    fs::recursive_directory_iterator iterator(
        packageRoot, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; iterator != end && !ec; iterator.increment(ec))
    {
        const fs::path path = iterator->path();
        if (fs::is_symlink(path, ec))
        {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!fs::is_regular_file(path, ec))
            continue;

        const fs::path relativePath = fs::relative(path, packageRoot, ec);
        if (ec || !IsSafeRelativePath(relativePath))
        {
            AddError(result, "Cannot resolve package file path: " + PathToUtf8(path));
            return false;
        }

        const std::string archiveName = relativePath.generic_string();
        const bool isLuau = IsLuauSource(relativePath) &&
            Lowercase(archiveName) != ".config.luau" && !IsUnderTests(archiveName);
        const bool isRuntimeLibrary = IsLibraryPath(archiveName) &&
            IsRuntimeLibrary(relativePath);
        if (isLuau || isRuntimeLibrary)
            collected.emplace(archiveName, path);
    }
    if (ec)
    {
        AddError(result, "Cannot enumerate package files: " + ec.message());
        return false;
    }

    files.reserve(collected.size());
    for (const auto& [archiveName, source] : collected)
        files.push_back(PackageFile{ source, archiveName });
    return result.IsValid();
}

bool WriteChecksum(const fs::path& checksumPath,
                   const fs::path& archivePath,
                   const std::string& sha256,
                   PackResult& result)
{
    const fs::path temporary = checksumPath.string() + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        AddError(result, "Cannot create package checksum: " + PathToUtf8(checksumPath));
        return false;
    }
    file << sha256 << "  " << archivePath.filename().string() << "\n";
    file.close();
    if (!file)
    {
        AddError(result, "Cannot write package checksum: " + PathToUtf8(checksumPath));
        std::error_code ec;
        fs::remove(temporary, ec);
        return false;
    }

    std::error_code ec;
    fs::remove(checksumPath, ec);
    ec.clear();
    fs::rename(temporary, checksumPath, ec);
    if (ec)
    {
        AddError(result, "Cannot finalize package checksum: " + ec.message());
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

fs::path ResolveOutputPath(const fs::path& packageRoot,
                           const json& manifest,
                           const fs::path& requested,
                           PackResult& result)
{
    if (!manifest.contains("name") || !manifest["name"].is_string() ||
        !manifest.contains("version") || !manifest["version"].is_string())
    {
        AddError(result, "Cannot determine package archive name from lode.json.");
        return {};
    }

    const std::string name = manifest["name"].get<std::string>();
    const std::string version = manifest["version"].get<std::string>();
    if (!IsSafeArchiveComponent(name) || !IsSafeArchiveComponent(version))
    {
        AddError(result, "Package name and version cannot contain path separators.");
        return {};
    }

    const fs::path output = requested.empty()
        ? packageRoot / "out" / ("lode-" + name + "-" + version + "-windows-x64.zip")
        : requested;
    return fs::absolute(output);
}

void RemoveOutputFiles(const fs::path& archivePath, const fs::path& checksumPath)
{
    std::error_code ec;
    fs::remove(archivePath, ec);
    fs::remove(checksumPath, ec);
}

} // namespace

PackResult PackPackage(const fs::path& packageRoot,
                       const fs::path& standardLibraryRoot,
                       const fs::path& outputArchive)
{
    PackResult result;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(packageRoot, ec), ec);
    if (ec || !fs::is_directory(root))
    {
        AddError(result, "Package root is not a directory: " + PathToUtf8(packageRoot));
        return result;
    }

    json manifest;
    if (!ReadManifest(root, manifest, result))
        return result;

    const bool hasLockfile = fs::is_regular_file(root / "lode.lock", ec);
    const ValidationReport validation = hasLockfile
        ? ValidateLockedPackage(root, standardLibraryRoot, true,
            ValidationMode::LockedArtifact)
        : Validate(root, ValidationMode::Artifact, standardLibraryRoot);
    if (!validation.IsValid())
    {
        result.errors = validation.errors;
        return result;
    }
    if (hasLockfile)
    {
        const LockfileResult lock = ValidateLockfile(root / "lode.lock",
                                                     validation.dependencyGraph);
        if (!lock.IsValid())
        {
            result.errors = lock.errors;
            return result;
        }
    }

    const fs::path archivePath = ResolveOutputPath(root, manifest, outputArchive, result);
    if (archivePath.empty())
        return result;
    const fs::path checksumPath = archivePath.string() + ".sha256";
    result.archivePath = archivePath;
    result.checksumPath = checksumPath;

    if (fs::is_directory(archivePath, ec) || fs::is_directory(checksumPath, ec))
    {
        AddError(result, "Package output path is a directory.");
        return result;
    }

    std::vector<PackageFile> files;
    if (!CollectPackageFiles(root, files, result))
        return result;

    fs::create_directories(archivePath.parent_path(), ec);
    if (ec)
    {
        AddError(result, "Cannot create package output directory: " + ec.message());
        return result;
    }
    RemoveOutputFiles(archivePath, checksumPath);

    mz_zip_archive archive{};
    const std::string archiveUtf8 = PathToUtf8(archivePath);
    if (!mz_zip_writer_init_file(&archive, archiveUtf8.c_str(), 0))
    {
        AddError(result, "Cannot create package archive: " + PathToUtf8(archivePath));
        return result;
    }

    bool valid = true;
    for (const PackageFile& file : files)
    {
        const std::string sourceUtf8 = PathToUtf8(file.source);
        if (!mz_zip_writer_add_file(&archive, file.archiveName.c_str(), sourceUtf8.c_str(),
                                    nullptr, 0, MZ_BEST_COMPRESSION))
        {
            AddError(result, "Cannot add package file to archive: " + file.archiveName);
            valid = false;
            break;
        }
    }
    if (valid && !mz_zip_writer_finalize_archive(&archive))
    {
        AddError(result, "Cannot finalize package archive: " + PathToUtf8(archivePath));
        valid = false;
    }
    mz_zip_writer_end(&archive);
    if (!valid)
    {
        RemoveOutputFiles(archivePath, checksumPath);
        return result;
    }

    try
    {
        const std::string sha256 = Lode::Detail::Sha256FileHex(archivePath);
        if (!WriteChecksum(checksumPath, archivePath, sha256, result))
        {
            RemoveOutputFiles(archivePath, checksumPath);
            return result;
        }

        const fs::path extractionRoot = fs::temp_directory_path() /
            ("lode-pack-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const ArchiveExtractionResult extraction = ExtractVerifiedArchive(
            archivePath, sha256, extractionRoot);
        if (!extraction.IsValid())
        {
            result.errors.insert(result.errors.end(),
                extraction.errors.begin(), extraction.errors.end());
            RemoveOutputFiles(archivePath, checksumPath);
            return result;
        }

        const ValidationReport extractedValidation = Validate(
            extraction.packageRoot, ValidationMode::InstallArtifact);
        fs::remove_all(extractionRoot, ec);
        if (!extractedValidation.IsValid())
        {
            result.errors.insert(result.errors.end(),
                extractedValidation.errors.begin(), extractedValidation.errors.end());
            RemoveOutputFiles(archivePath, checksumPath);
            return result;
        }
    }
    catch (const std::exception& error)
    {
        AddError(result, error.what());
        RemoveOutputFiles(archivePath, checksumPath);
    }

    return result;
}

} // namespace Lode::Package
