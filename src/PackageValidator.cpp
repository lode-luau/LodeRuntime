// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageValidator.hpp"

#include "PathUtil.hpp"
#include "Platform/Platform.hpp"
#include "Lode/Export.hpp"
#include "nlohmann/json.hpp"

#include <fstream>
#include <cstring>
#include <array>
#include <optional>
#include <regex>
#include <map>
#include <set>

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

bool IsVersionRequirement(const std::string& value)
{
    if (IsSemVer(value))
        return true;

    static const std::regex rangePattern(
        R"(^[\^~][0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)"
    );
    return std::regex_match(value, rangePattern);
}

void ValidateDependencyTable(const json& dependencies,
                             const std::string& fieldName,
                             ValidationReport& report)
{
    if (!dependencies.is_object())
    {
        Error(report, "lode.json." + fieldName + " must be an object.");
        return;
    }

    for (const auto& [alias, declaration] : dependencies.items())
    {
        if (alias.empty())
        {
            Error(report, "lode.json." + fieldName + " contains an empty dependency alias.");
            continue;
        }

        if (declaration.is_string())
        {
            if (!IsSemVer(declaration.get<std::string>()))
            {
                Error(report, "lode.json." + fieldName + "." + alias +
                    " must use an exact SemVer string for an official standard module.");
            }
            continue;
        }

        if (!declaration.is_object())
        {
            Error(report, "lode.json." + fieldName + "." + alias +
                " must be an exact version string or a source object.");
            continue;
        }

        if (declaration.contains("alias"))
        {
            Error(report, "lode.json." + fieldName + "." + alias +
                " must not contain an alias field; the dependency key is the alias.");
        }

        const bool hasGit = declaration.contains("git");
        const bool hasPath = declaration.contains("path");
        if (hasGit == hasPath)
        {
            Error(report, "lode.json." + fieldName + "." + alias +
                " must contain exactly one of git or path.");
        }

        const char* sourceField = hasGit ? "git" : "path";
        if (declaration.contains(sourceField) && !declaration[sourceField].is_string())
        {
            Error(report, "lode.json." + fieldName + "." + alias + "." + sourceField +
                " must be a string.");
        }

        if (!declaration.contains("version") || !declaration["version"].is_string() ||
            !IsVersionRequirement(declaration["version"].get<std::string>()))
        {
            Error(report, "lode.json." + fieldName + "." + alias +
                ".version must be a SemVer or a ^/~ SemVer requirement.");
        }
    }
}

struct DependencyGraphContext
{
    DependencyGraph graph;
    std::set<fs::path> activePackages;
    std::map<fs::path, size_t> packageIndexes;
    fs::path standardLibraryRoot;
};

std::optional<fs::path> FindStdlibRoot(const fs::path& packageRoot,
                                       const fs::path& standardLibraryRoot)
{
    std::error_code ec;
    if (!standardLibraryRoot.empty() && fs::is_directory(standardLibraryRoot, ec))
        return fs::weakly_canonical(standardLibraryRoot, ec);

    fs::path current = packageRoot;
    while (!current.empty())
    {
        if (current.filename() == "modules" && fs::is_directory(current))
            return current;

        const fs::path siblingModules = current / "modules";
        if (fs::is_directory(siblingModules))
            return siblingModules;

        const fs::path parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }

    return std::nullopt;
}

bool FindStdlibManifest(const fs::path& stdlibRoot,
                        const std::string& name,
                        fs::path& packageRoot,
                        json& manifest)
{
    std::error_code ec;
    if (!fs::is_directory(stdlibRoot, ec))
        return false;

    for (fs::recursive_directory_iterator it(
             stdlibRoot, fs::directory_options::follow_directory_symlink, ec),
         end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || it->path().filename() != "lode.json")
            continue;

        std::ifstream file(it->path());
        if (!file.is_open())
            continue;

        try
        {
            json candidate = json::parse(file);
            if (candidate.is_object() && candidate.value("name", "") == name)
            {
                packageRoot = it->path().parent_path();
                manifest = std::move(candidate);
                return true;
            }
        }
        catch (const std::exception&)
        {
            // The package validator reports malformed manifests when they are
            // selected; unrelated catalog entries are not part of this lookup.
        }
    }

    return false;
}

std::optional<std::array<int, 3>> ParseVersionCore(const std::string& value)
{
    static const std::regex pattern(R"(^([0-9]+)\.([0-9]+)\.([0-9]+))");
    std::smatch match;
    if (!std::regex_search(value, match, pattern))
        return std::nullopt;

    return std::array<int, 3>{
        std::stoi(match[1].str()),
        std::stoi(match[2].str()),
        std::stoi(match[3].str())
    };
}

bool VersionSatisfies(const std::string& actual, const std::string& requirement)
{
    if (IsSemVer(requirement))
        return actual == requirement;

    if (requirement.empty() || (requirement[0] != '^' && requirement[0] != '~'))
        return false;

    const auto actualCore = ParseVersionCore(actual);
    const auto requiredCore = ParseVersionCore(requirement.substr(1));
    if (!actualCore || !requiredCore)
        return false;

    if (*actualCore < *requiredCore)
        return false;

    if (requirement[0] == '~')
        return (*actualCore)[0] == (*requiredCore)[0] &&
            (*actualCore)[1] == (*requiredCore)[1];

    if ((*requiredCore)[0] > 0)
        return (*actualCore)[0] == (*requiredCore)[0];
    if ((*requiredCore)[1] > 0)
        return (*actualCore)[0] == 0 && (*actualCore)[1] == (*requiredCore)[1];
    return (*actualCore)[0] == 0 && (*actualCore)[1] == 0 &&
        (*actualCore)[2] == (*requiredCore)[2];
}

bool ReadDependencyManifest(const fs::path& packageRoot,
                            json& manifest,
                            ValidationReport& report)
{
    const fs::path manifestPath = packageRoot / "lode.json";
    std::ifstream file(manifestPath);
    if (!file.is_open())
    {
        Error(report, "Missing dependency manifest: " + PathToUtf8(manifestPath));
        return false;
    }

    try
    {
        manifest = json::parse(file);
    }
    catch (const std::exception& exception)
    {
        Error(report, "Failed to parse dependency manifest " + PathToUtf8(manifestPath) + ": " + exception.what());
        return false;
    }

    if (!manifest.is_object())
    {
        Error(report, "Dependency manifest must contain a JSON object: " + PathToUtf8(manifestPath));
        return false;
    }

    return true;
}

size_t AddGraphNode(const fs::path& packageRoot,
                    const json& manifest,
                    DependencySource source,
                    DependencyGraphContext& context)
{
    const fs::path canonicalRoot = fs::weakly_canonical(packageRoot);
    auto existing = context.packageIndexes.find(canonicalRoot);
    if (existing != context.packageIndexes.end())
        return existing->second;

    const size_t index = context.graph.packages.size();
    context.packageIndexes.emplace(canonicalRoot, index);
    context.graph.packages.push_back(PackageNode{
        manifest.value("name", ""),
        manifest.value("version", ""),
        canonicalRoot,
        source,
        {}
    });
    return index;
}

void ResolveDependencyGraph(const fs::path& packageRoot,
                            const json& manifest,
                            DependencySource source,
                            DependencyGraphContext& context,
                            ValidationReport& report);

void ResolveDependencyTable(const fs::path& packageRoot,
                            const json& manifest,
                            DependencySource source,
                            const char* fieldName,
                            DependencyScope scope,
                            DependencyGraphContext& context,
                            ValidationReport& report)
{
    const size_t packageIndex = AddGraphNode(packageRoot, manifest, source, context);
    if (!manifest.contains(fieldName) || !manifest[fieldName].is_object())
        return;

    for (const auto& [alias, declaration] : manifest[fieldName].items())
    {
        if (declaration.is_object() && declaration.contains("path") && declaration["path"].is_string())
        {
            DependencyEdge edge{
                alias,
                DependencySource::Path,
                declaration.value("version", ""),
                declaration["path"].get<std::string>(),
                std::nullopt,
                scope
            };
            std::error_code ec;
            const fs::path dependencyRoot = fs::weakly_canonical(
                fs::absolute(packageRoot / PathFromUtf8(declaration["path"].get<std::string>()), ec), ec);
            if (ec || !fs::is_directory(dependencyRoot))
            {
                Error(report, "Path dependency '" + alias + "' does not point to a directory.");
                context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
                continue;
            }

            json dependencyManifest;
            if (!ReadDependencyManifest(dependencyRoot, dependencyManifest, report))
            {
                context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
                continue;
            }

            const size_t dependencyIndex = AddGraphNode(
                dependencyRoot, dependencyManifest, DependencySource::Path, context);
            edge.target = dependencyIndex;
            context.graph.packages[packageIndex].dependencies.push_back(edge);

            const std::string requestedVersion = declaration.value("version", "");
            const std::string availableVersion = dependencyManifest.value("version", "");
            if (!VersionSatisfies(availableVersion, requestedVersion))
            {
                Error(report, "Path dependency '" + alias + "' requires version " +
                    requestedVersion + ", but the package contains " + availableVersion + ".");
                continue;
            }

            if (context.activePackages.find(dependencyRoot) == context.activePackages.end() &&
                context.graph.packages[dependencyIndex].dependencies.empty())
                ResolveDependencyGraph(dependencyRoot, dependencyManifest, DependencySource::Path, context, report);
            continue;
        }

        if (declaration.is_object() && declaration.contains("git") && declaration["git"].is_string())
        {
            context.graph.packages[packageIndex].dependencies.push_back(DependencyEdge{
                alias,
                DependencySource::Git,
                declaration.value("version", ""),
                declaration["git"].get<std::string>(),
                std::nullopt,
                scope
            });
            continue;
        }

        if (!declaration.is_string())
            continue;

        const std::string requestedVersion = declaration.get<std::string>();
        DependencyEdge edge{
            alias,
            DependencySource::StandardLibrary,
            requestedVersion,
            "",
            std::nullopt,
            scope
        };
        const auto stdlibRoot = FindStdlibRoot(packageRoot, context.standardLibraryRoot);
        if (!stdlibRoot)
        {
            context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
            continue;
        }

        fs::path dependencyRoot;
        json dependencyManifest;
        if (!FindStdlibManifest(*stdlibRoot, alias, dependencyRoot, dependencyManifest))
        {
            Error(report, "Standard library dependency '" + alias + "' was not found in " +
                PathToUtf8(*stdlibRoot) + ".");
            context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
            continue;
        }

        const size_t dependencyIndex = AddGraphNode(
            dependencyRoot, dependencyManifest, DependencySource::StandardLibrary, context);
        edge.target = dependencyIndex;
        context.graph.packages[packageIndex].dependencies.push_back(edge);

        const std::string availableVersion = dependencyManifest.value("version", "");
        if (availableVersion != requestedVersion)
        {
            Error(report, "Standard library dependency '" + alias + "' requires version " +
                requestedVersion + ", but the repository contains " + availableVersion + ".");
            continue;
        }

        if (context.activePackages.find(dependencyRoot) == context.activePackages.end() &&
            context.graph.packages[dependencyIndex].dependencies.empty())
            ResolveDependencyGraph(dependencyRoot, dependencyManifest, DependencySource::StandardLibrary, context, report);
    }

}

void ResolveDependencyGraph(const fs::path& packageRoot,
                            const json& manifest,
                            DependencySource source,
                            DependencyGraphContext& context,
                            ValidationReport& report)
{
    const fs::path canonicalRoot = fs::weakly_canonical(packageRoot);
    AddGraphNode(packageRoot, manifest, source, context);

    const bool hasRuntimeDependencies = manifest.contains("dependencies") &&
        manifest["dependencies"].is_object();
    const bool hasRootDevelopmentDependencies = source == DependencySource::Root &&
        manifest.contains("devDependencies") && manifest["devDependencies"].is_object();
    if (!hasRuntimeDependencies && !hasRootDevelopmentDependencies)
        return;

    if (!context.activePackages.insert(canonicalRoot).second)
    {
        Error(report, "Dependency cycle detected at " + PathToUtf8(canonicalRoot));
        return;
    }

    ResolveDependencyTable(packageRoot, manifest, source, "dependencies",
        DependencyScope::Runtime, context, report);
    if (source == DependencySource::Root)
    {
        ResolveDependencyTable(packageRoot, manifest, source, "devDependencies",
            DependencyScope::Development, context, report);
    }

    context.activePackages.erase(canonicalRoot);
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
    return Validate(packageRoot, mode, {});
}

ValidationReport Validate(const fs::path& packageRoot,
                          ValidationMode mode,
                          const fs::path& standardLibraryRoot)
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

    if (manifest.contains("dependencies"))
        ValidateDependencyTable(manifest["dependencies"], "dependencies", report);
    if (manifest.contains("devDependencies"))
        ValidateDependencyTable(manifest["devDependencies"], "devDependencies", report);

    DependencyGraphContext context;
    context.standardLibraryRoot = standardLibraryRoot;
    ResolveDependencyGraph(root, manifest, DependencySource::Root, context, report);
    report.dependencyGraph = std::move(context.graph);

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
