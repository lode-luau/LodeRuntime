// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageValidator.hpp"

#include "PathUtil.hpp"
#include "Platform/Platform.hpp"
#include "Lode/Export.hpp"
#include "nlohmann/json.hpp"

#include <fstream>
#include <cstring>
#include <regex>

namespace fs = std::filesystem;

namespace Lode::Package
{

namespace
{

using json = nlohmann::json;
using Lode::Detail::PathToUtf8;
using Lode::Detail::PathFromUtf8;

void Error(ValidationReport& report, std::string message)
{
    report.errors.push_back(std::move(message));
}

void Warning(ValidationReport& report, std::string message)
{
    report.warnings.push_back(std::move(message));
}

bool IsPathInside(const fs::path& candidate, const fs::path& root)
{
    std::error_code ec;
    fs::path relative = fs::relative(fs::weakly_canonical(candidate, ec), fs::weakly_canonical(root, ec), ec);
    if (ec)
        return false;

    if (relative.empty() || relative == ".")
        return true;

    auto it = relative.begin();
    return it == relative.end() || *it != "..";
}

bool IsSemVer(const std::string& value)
{
    static const std::regex pattern(
        R"(^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$)"
    );
    return std::regex_match(value, pattern);
}

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void CheckSymbol(const std::shared_ptr<Platform::DynamicLibrary>& library,
                 const fs::path& artifact,
                 std::string_view symbol,
                 ValidationReport& report)
{
    if (library->GetSymbol(symbol).IsError())
    {
        Error(report, PathToUtf8(artifact) + " is missing required symbol " + std::string(symbol) + ".");
    }
}

void ValidateArtifact(const fs::path& artifact,
                      const std::string& configuration,
                      ValidationReport& report)
{
    auto result = Platform::DynamicLibrary::Open(PathToUtf8(artifact));
    if (result.IsError())
    {
        Error(report, "Failed to load native artifact " + PathToUtf8(artifact) + ": " + result.GetError().ErrorMessage());
        return;
    }

    std::shared_ptr<Platform::DynamicLibrary> library = result.GetValue();
    CheckSymbol(library, artifact, "LodeModuleInit", report);
    CheckSymbol(library, artifact, "LodeModuleConfig", report);
    CheckSymbol(library, artifact, "LodeModuleABI", report);

    auto configResult = library->GetSymbol("LodeModuleConfig");
    if (configResult.IsOk())
    {
        using ConfigFunction = const char* (*)();
        auto configFunction = reinterpret_cast<ConfigFunction>(configResult.GetValue());
        const char* moduleConfiguration = configFunction ? configFunction() : nullptr;
        if (!moduleConfiguration || std::strcmp(moduleConfiguration, configuration.c_str()) != 0)
        {
            Error(report, PathToUtf8(artifact) + " reports configuration '" +
                (moduleConfiguration ? moduleConfiguration : "<missing>") +
                "' but the artifact path requires '" + configuration + "'.");
        }
    }

    auto abiResult = library->GetSymbol("LodeModuleABI");
    if (abiResult.IsOk())
    {
        using AbiFunction = const char* (*)();
        auto abiFunction = reinterpret_cast<AbiFunction>(abiResult.GetValue());
        const char* moduleAbi = abiFunction ? abiFunction() : nullptr;
        if (!moduleAbi || std::strcmp(moduleAbi, LodeAbiId()) != 0)
        {
            Error(report, PathToUtf8(artifact) + " reports ABI '" +
                (moduleAbi ? moduleAbi : "<missing>") +
                "' but the runtime requires '" + LodeAbiId() + "'.");
        }
    }
}

void ValidateLibraryEntry(const fs::path& root,
                          const std::string& platform,
                          const std::string& architecture,
                          const std::string& relativePath,
                          ValidationMode mode,
                          bool& hasThirdPartyRuntime,
                          ValidationReport& report)
{
    fs::path relative = PathFromUtf8(relativePath);
    fs::path basePath = root / relative;

    if (relativePath.empty() || relative.is_absolute() || !IsPathInside(basePath, root))
    {
        Error(report, "libraries." + platform + "." + architecture +
            " must be a non-empty relative path inside the package.");
        return;
    }

    const fs::path parent = basePath.parent_path();
    if (mode == ValidationMode::Source)
        return;

    const fs::path filename = basePath.filename();
    const std::vector<std::string> configurations = { "Debug", "Release" };
    for (const std::string& configuration : configurations)
    {
        fs::path artifact = parent / configuration / filename;
        if (!fs::is_regular_file(artifact))
        {
            Error(report, "Missing " + configuration + " native artifact: " + PathToUtf8(artifact));
            continue;
        }

        ValidateArtifact(artifact, configuration, report);

        for (const auto& entry : fs::directory_iterator(artifact.parent_path()))
        {
            std::string name = entry.path().filename().string();
            if (name.rfind("libcrypto", 0) == 0 || name.rfind("libssl", 0) == 0)
                hasThirdPartyRuntime = true;
        }
    }
}

} // namespace

ValidationReport Validate(const fs::path& packageRoot)
{
    return Validate(packageRoot, ValidationMode::Artifact);
}

ValidationReport Validate(const fs::path& packageRoot, ValidationMode mode)
{
    ValidationReport report;
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(packageRoot, ec), ec);
    if (ec || !fs::is_directory(root))
    {
        Error(report, "Package root is not a directory: " + PathToUtf8(packageRoot));
        return report;
    }

    const fs::path manifestPath = root / "lode.json";
    if (!fs::is_regular_file(manifestPath))
    {
        Error(report, "Missing package manifest: " + PathToUtf8(manifestPath));
        return report;
    }

    json manifest;
    try
    {
        std::ifstream file(manifestPath);
        manifest = json::parse(file);
    }
    catch (const std::exception& exception)
    {
        Error(report, "Failed to parse " + PathToUtf8(manifestPath) + ": " + exception.what());
        return report;
    }

    if (!manifest.is_object())
    {
        Error(report, "lode.json must contain a JSON object.");
        return report;
    }

    if (!manifest.contains("name") || !manifest["name"].is_string() || manifest["name"].get<std::string>().empty())
        Error(report, "lode.json.name must be a non-empty string.");

    if (!manifest.contains("version") || !manifest["version"].is_string() ||
        !IsSemVer(manifest["version"].get<std::string>()))
        Error(report, "lode.json.version must be a valid SemVer string.");

    if (!fs::is_regular_file(root / "init.luau"))
        Error(report, "Packages must contain a root init.luau file.");

    if (!fs::is_regular_file(root / "LICENSE"))
        Error(report, "Packages must contain a root LICENSE file.");

    if (!manifest.contains("libraries") || !manifest["libraries"].is_object())
    {
        Warning(report, "Package has no native libraries; native ABI validation was skipped.");
        return report;
    }

    if (mode == ValidationMode::Source && !fs::is_regular_file(root / "CMakeLists.txt"))
        Error(report, "Native source packages must contain a root CMakeLists.txt file.");

    bool hasThirdPartyRuntime = false;
    for (auto platformIt = manifest["libraries"].begin(); platformIt != manifest["libraries"].end(); ++platformIt)
    {
        if (!platformIt.value().is_object())
        {
            Error(report, "libraries." + platformIt.key() + " must be an object.");
            continue;
        }

        for (auto architectureIt = platformIt.value().begin(); architectureIt != platformIt.value().end(); ++architectureIt)
        {
            if (!architectureIt.value().is_string())
            {
                Error(report, "libraries." + platformIt.key() + "." + architectureIt.key() + " must be a string.");
                continue;
            }

            ValidateLibraryEntry(root, platformIt.key(), architectureIt.key(), architectureIt.value().get<std::string>(), mode,
                hasThirdPartyRuntime, report);
        }
    }

    if (hasThirdPartyRuntime && !fs::is_regular_file(root / "NOTICE"))
        Error(report, "Package bundles OpenSSL runtime files but is missing root NOTICE.");

    return report;
}

} // namespace Lode::Package
