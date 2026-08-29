// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageValidator.hpp"

#include "PackageManifest.hpp"

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
        Error(report, "package manifest." + fieldName + " must be an object.");
        return;
    }

    for (const auto& [alias, declaration] : dependencies.items())
    {
        if (alias.empty())
        {
            Error(report, "package manifest." + fieldName + " contains an empty dependency alias.");
            continue;
        }

        if (declaration.is_string())
        {
            if (!IsSemVer(declaration.get<std::string>()))
            {
                Error(report, "package manifest." + fieldName + "." + alias +
                    " must use an exact SemVer string for an official standard module.");
            }
            continue;
        }

        if (!declaration.is_object())
        {
            Error(report, "package manifest." + fieldName + "." + alias +
                " must be an exact version string or a source object.");
            continue;
        }

        if (declaration.contains("alias"))
        {
            Error(report, "package manifest." + fieldName + "." + alias +
                " must not contain an alias field; the dependency key is the alias.");
        }

        const bool hasGit = declaration.contains("git");
        const bool hasPath = declaration.contains("path");
        if (hasGit == hasPath)
        {
            Error(report, "package manifest." + fieldName + "." + alias +
                " must contain exactly one of git or path.");
        }

        const char* sourceField = hasGit ? "git" : "path";
        if (declaration.contains(sourceField) && !declaration[sourceField].is_string())
        {
            Error(report, "package manifest." + fieldName + "." + alias + "." + sourceField +
                " must be a string.");
        }

        if (!declaration.contains("version") || !declaration["version"].is_string() ||
            !IsVersionRequirement(declaration["version"].get<std::string>()))
        {
            Error(report, "package manifest." + fieldName + "." + alias +
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
    ValidationMode mode = ValidationMode::Source;
};

bool AllowsUnresolvedStdlib(ValidationMode mode)
{
    return mode == ValidationMode::InstallConsumer ||
        mode == ValidationMode::InstallSource ||
        mode == ValidationMode::InstallArtifact ||
        mode == ValidationMode::LockedArtifact;
}

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
        // A package may legitimately keep Luau modules under its own
        // modules/ directory. A package root with a manifest is not the
        // repository-level stdlib catalog, even when installation generated
        // a .config.luau beside it.
        if (fs::is_directory(siblingModules) &&
            fs::is_regular_file(current / ".config.luau") &&
            !fs::is_regular_file(current / "package.luau"))
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
        if (!it->is_regular_file(ec) || it->path().filename() != "package.luau")
            continue;

        try
        {
            const PackageManifestResult parsed = ReadPackageManifest(it->path());
            if (!parsed.IsValid())
                continue;
            json candidate = parsed.document;
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
    const fs::path packageManifestPath = packageRoot / "package.luau";
    if (!fs::is_regular_file(packageManifestPath))
    {
        Error(report, "Missing dependency manifest: " + PathToUtf8(packageManifestPath));
        return false;
    }

    const PackageManifestResult parsed = ReadPackageManifest(packageManifestPath);
    if (!parsed.IsValid())
    {
        report.errors.insert(report.errors.end(), parsed.errors.begin(), parsed.errors.end());
        return false;
    }
    manifest = parsed.document;
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
        "",
        "",
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
            if (!AllowsUnresolvedStdlib(context.mode))
            {
                Error(report, "Standard library dependency '" + alias +
                    "' cannot be resolved because no standard library catalog was found.");
            }
            context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
            continue;
        }

        fs::path dependencyRoot;
        json dependencyManifest;
        if (!FindStdlibManifest(*stdlibRoot, alias, dependencyRoot, dependencyManifest))
        {
            if (!AllowsUnresolvedStdlib(context.mode))
            {
                Error(report, "Standard library dependency '" + alias + "' was not found in " +
                    PathToUtf8(*stdlibRoot) + ".");
            }
            context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
            continue;
        }

        const std::string availableVersion = dependencyManifest.value("version", "");
        if (!VersionSatisfies(availableVersion, requestedVersion))
        {
            if (!AllowsUnresolvedStdlib(context.mode))
            {
                Error(report, "Standard library dependency '" + alias + "' requires version " +
                    requestedVersion + ", but the repository contains " + availableVersion + ".");
            }
            context.graph.packages[packageIndex].dependencies.push_back(std::move(edge));
            continue;
        }

        const size_t dependencyIndex = AddGraphNode(
            dependencyRoot, dependencyManifest, DependencySource::StandardLibrary, context);
        edge.target = dependencyIndex;
        context.graph.packages[packageIndex].dependencies.push_back(edge);

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

std::string ImplementationExtension(const std::string& platform)
{
    if (platform == "windows") return ".dll";
    if (platform == "macos" || platform == "ios") return ".dylib";
    return ".so";
}

std::string ExpandImplementationLayout(std::string layout,
                                       const std::string& platform,
                                       const std::string& architecture,
                                       const std::string& configuration,
                                       const std::string& artifact)
{
    const std::array<std::pair<std::string_view, std::string>, 5> substitutions{{
        { "{platform}", platform },
        { "{architecture}", architecture },
        { "{configuration}", configuration },
        { "{artifact}", artifact },
        { "{extension}", ImplementationExtension(platform) }
    }};
    for (const auto& [token, replacement] : substitutions)
    {
        size_t position = 0;
        while ((position = layout.find(token, position)) != std::string::npos)
        {
            layout.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    }
    return layout;
}

void ValidateImplementation(const fs::path& root,
                            const json& manifest,
                            ValidationMode mode,
                            bool& hasThirdPartyRuntime,
                            ValidationReport& report)
{
    const json& implementation = manifest["implementation"];
    if (!implementation.is_object())
    {
        Error(report, "implementation must be an object.");
        return;
    }

    const std::string packageName = manifest.value("name", "");
    std::string artifact = packageName;
    if (implementation.contains("artifact"))
    {
        if (!implementation["artifact"].is_string())
        {
            Error(report, "implementation.artifact must be a non-empty string.");
            return;
        }
        artifact = implementation["artifact"].get<std::string>();
    }
    if (artifact.empty())
    {
        Error(report, "implementation.artifact must be a non-empty string.");
        return;
    }

    std::string layout =
        "libs/{platform}/{architecture}/{configuration}/{artifact}{extension}";
    if (implementation.contains("layout"))
    {
        if (!implementation["layout"].is_string())
        {
            Error(report, "implementation.layout must be a string.");
            return;
        }
        layout = implementation["layout"].get<std::string>();
    }

    if (!implementation.contains("targets") || !implementation["targets"].is_object())
    {
        Error(report, "Native packages must declare implementation.targets.");
        return;
    }
    const json& targets = implementation["targets"];
    if (!targets.contains("release") || !targets["release"].is_array() || targets["release"].empty())
    {
        Error(report, "implementation.targets.release must be a non-empty array.");
        return;
    }

    std::set<std::pair<std::string, std::string>> publishTargets;
    for (const json& target : targets["release"])
    {
        if (!target.is_string())
        {
            Error(report, "implementation.targets.release entries must be strings in platform/architecture form.");
            continue;
        }
        const std::string value = target.get<std::string>();
        const size_t separator = value.find('/');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size() ||
            value.find('/', separator + 1) != std::string::npos)
        {
            Error(report, "implementation.targets.release entry '" + value +
                "' must use platform/architecture form.");
            continue;
        }
        publishTargets.emplace(value.substr(0, separator), value.substr(separator + 1));
    }

    const bool sourceValidation = mode == ValidationMode::Source ||
        mode == ValidationMode::InstallConsumer || mode == ValidationMode::InstallSource;
    if (sourceValidation)
        return;

    for (const auto& [platform, architecture] : publishTargets)
    {
        if (mode == ValidationMode::InstallArtifact &&
            (platform != Platform::GetOSName() ||
             architecture != Platform::GetArchitectureName()))
            continue;

        const std::vector<std::string> configurations = mode == ValidationMode::InstallArtifact
            ? std::vector<std::string>{ "Release" }
            : std::vector<std::string>{ "Debug", "Release" };
        for (const std::string& configuration : configurations)
        {
            const fs::path relative = PathFromUtf8(ExpandImplementationLayout(
                layout, platform, architecture, configuration, artifact));
            const fs::path artifactPath = root / relative;
            if (relative.is_absolute() || !IsPathInside(artifactPath, root))
            {
                Error(report, "implementation.layout must resolve inside the package.");
                continue;
            }
            if (!fs::is_regular_file(artifactPath))
            {
                Error(report, "Missing " + configuration + " native artifact: " + PathToUtf8(artifactPath));
                continue;
            }
            ValidateArtifact(artifactPath, configuration, report);
            for (const auto& entry : fs::directory_iterator(artifactPath.parent_path()))
            {
                const std::string name = entry.path().filename().string();
                if (name.rfind("libcrypto", 0) == 0 || name.rfind("libssl", 0) == 0)
                    hasThirdPartyRuntime = true;
            }
        }
    }
}

} // namespace

bool PackageVersionSatisfies(const std::string& actual,
                             const std::string& requirement)
{
    return VersionSatisfies(actual, requirement);
}

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

    const fs::path manifestPath = root / "package.luau";
    if (!fs::is_regular_file(manifestPath))
    {
        Error(report, "Missing package manifest: " + PathToUtf8(manifestPath));
        return report;
    }

    const PackageManifestResult parsed = ReadPackageManifest(manifestPath);
    if (!parsed.IsValid())
    {
        report.errors.insert(report.errors.end(), parsed.errors.begin(), parsed.errors.end());
        return report;
    }
    json manifest = parsed.document;

    if (!manifest.is_object())
    {
        Error(report, "Package manifest must contain an object.");
        return report;
    }

    if (manifest.contains("dependencies"))
        ValidateDependencyTable(manifest["dependencies"], "dependencies", report);
    if (manifest.contains("devDependencies"))
        ValidateDependencyTable(manifest["devDependencies"], "devDependencies", report);

    DependencyGraphContext context;
    context.standardLibraryRoot = standardLibraryRoot;
    context.mode = mode;
    ResolveDependencyGraph(root, manifest, DependencySource::Root, context, report);
    report.dependencyGraph = std::move(context.graph);

    if (!manifest.contains("name") || !manifest["name"].is_string() || manifest["name"].get<std::string>().empty())
        Error(report, "package manifest.name must be a non-empty string.");

    if (!manifest.contains("version") || !manifest["version"].is_string() ||
        !IsSemVer(manifest["version"].get<std::string>()))
        Error(report, "package manifest.version must be a valid SemVer string.");

    const bool consumerWithoutEntrypoint = mode == ValidationMode::InstallConsumer &&
        !manifest.contains("implementation");
    if (!consumerWithoutEntrypoint && !fs::is_regular_file(root / "init.luau"))
        Error(report, "Packages must contain a root init.luau file.");

    if (!fs::is_regular_file(root / "LICENSE"))
    {
        if (mode == ValidationMode::InstallConsumer ||
            mode == ValidationMode::InstallSource || mode == ValidationMode::InstallArtifact)
            Warning(report, "Package has no LICENSE file; run `lode init --license <SPDX>` to add one.");
        else
            Error(report, "Packages must contain a root LICENSE file.");
    }

    for (const char* legacyField : { "libraries", "releaseTargets" })
    {
        if (manifest.contains(legacyField))
            Error(report, "package manifest field '" + std::string(legacyField) +
                "' is no longer supported; use implementation.");
    }

    if (manifest.contains("implementation"))
    {
        if (!manifest["implementation"].is_object())
        {
            Error(report, "implementation must be an object.");
            return report;
        }
        const bool sourceValidation = mode == ValidationMode::Source ||
            mode == ValidationMode::InstallConsumer || mode == ValidationMode::InstallSource;
        if (sourceValidation && !fs::is_regular_file(root / "CMakeLists.txt"))
            Error(report, "Native source packages must contain a root CMakeLists.txt file.");
        bool hasThirdPartyRuntime = false;
        ValidateImplementation(root, manifest, mode, hasThirdPartyRuntime, report);
        return report;
    }
    Warning(report, "Package has no native implementation; native ABI validation was skipped.");
    return report;
}

} // namespace Lode::Package
