// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageInstaller.hpp"

#include "PackageCache.hpp"
#include "PackageConfig.hpp"
#include "PackageLockfile.hpp"
#include "PackageValidator.hpp"
#include "GitResolver.hpp"
#include "PackageArtifact.hpp"
#include "PathUtil.hpp"
#include "nlohmann/json.hpp"

#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathToUtf8;
using json = nlohmann::json;

void AddError(InstallResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

std::vector<bool> CollectInstallablePackages(const DependencyGraph& graph,
                                              bool includeDevelopmentDependencies)
{
    std::vector<bool> installable(graph.packages.size(), false);
    if (graph.root >= graph.packages.size())
        return installable;

    installable[graph.root] = true;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t packageIndex = 0; packageIndex < graph.packages.size(); ++packageIndex)
        {
            if (!installable[packageIndex])
                continue;

            for (const DependencyEdge& edge : graph.packages[packageIndex].dependencies)
            {
                if (edge.scope == DependencyScope::Development &&
                    (!includeDevelopmentDependencies || packageIndex != graph.root))
                    continue;
                if (edge.target && *edge.target < graph.packages.size() &&
                    !installable[*edge.target])
                {
                    installable[*edge.target] = true;
                    changed = true;
                }
            }
        }
    }

    return installable;
}

bool IsInstallEdge(const DependencyGraph& graph,
                   size_t packageIndex,
                   const DependencyEdge& edge,
                   const std::vector<bool>& installablePackages,
                   bool includeDevelopmentDependencies)
{
    if (packageIndex >= installablePackages.size() || !installablePackages[packageIndex])
        return false;
    if (edge.scope == DependencyScope::Development)
        return includeDevelopmentDependencies && packageIndex == graph.root;
    return true;
}

bool HasInstallEdges(const DependencyGraph& graph,
                     const std::vector<bool>& installablePackages,
                     bool includeDevelopmentDependencies)
{
    for (size_t packageIndex = 0; packageIndex < graph.packages.size(); ++packageIndex)
    {
        for (const DependencyEdge& edge : graph.packages[packageIndex].dependencies)
        {
            if (IsInstallEdge(graph, packageIndex, edge, installablePackages,
                              includeDevelopmentDependencies))
                return true;
        }
    }
    return false;
}

struct LockedGitRecord
{
    std::string name;
    std::string version;
    std::string repository;
    std::string commit;
    std::vector<PackageArtifact> artifacts;
};

std::string LockedEdgeKey(const json& package, const json& dependency)
{
    return package.value("name", "") + "\n" +
        package.value("version", "") + "\n" +
        dependency.value("alias", "") + "\n" +
        dependency.value("requirement", "");
}

bool ParseLockedGitEdges(const json& document,
                         std::map<std::string, LockedGitRecord>& records,
                         InstallResult& result)
{
    if (!document.is_object() || !document.contains("packages") ||
        !document["packages"].is_array())
    {
        AddError(result, "Locked package installation requires a valid lode.lock packages array.");
        return false;
    }

    const json& packages = document["packages"];
    for (const json& package : packages)
    {
        if (!package.is_object())
            continue;
        for (const char* field : { "dependencies", "devDependencies" })
        {
            if (!package.contains(field) || !package[field].is_array())
                continue;
            for (const json& dependency : package[field])
            {
                if (!dependency.is_object() || !dependency.contains("target") ||
                    !dependency["target"].is_number_unsigned())
                    continue;
                const size_t target = dependency["target"].get<size_t>();
                if (target >= packages.size() || !packages[target].is_object() ||
                    packages[target].value("source", "") != "git")
                    continue;

                const json& targetPackage = packages[target];
                LockedGitRecord record;
                record.name = targetPackage.value("name", "");
                record.version = targetPackage.value("version", "");
                record.repository = targetPackage.value("reference", "");
                record.commit = targetPackage.value("commit", "");
                if (record.name.empty() || record.version.empty() ||
                    record.repository.empty() || record.commit.empty())
                {
                    AddError(result, "Locked Git package record is missing name, version, reference, or commit.");
                    return false;
                }

                if (targetPackage.contains("artifacts"))
                {
                    if (!targetPackage["artifacts"].is_array())
                    {
                        AddError(result, "Locked Git package artifacts must be an array.");
                        return false;
                    }
                    for (const json& artifactDocument : targetPackage["artifacts"])
                    {
                        if (!artifactDocument.is_object())
                            continue;
                        PackageArtifact artifact;
                        artifact.platform = artifactDocument.value("platform", "");
                        artifact.architecture = artifactDocument.value("architecture", "");
                        artifact.configuration = artifactDocument.value("configuration", "");
                        artifact.abi = artifactDocument.value("abi", "");
                        artifact.release = artifactDocument.value("release", "");
                        artifact.asset = artifactDocument.value("asset", "");
                        artifact.sha256 = artifactDocument.value("sha256", "");
                        record.artifacts.push_back(std::move(artifact));
                    }
                }

                const std::string key = LockedEdgeKey(package, dependency);
                const auto existing = records.find(key);
                if (existing != records.end() &&
                    (existing->second.repository != record.repository ||
                     existing->second.commit != record.commit))
                {
                    AddError(result, "lode.lock contains ambiguous Git resolutions for dependency '" +
                        dependency.value("alias", "") + "'.");
                    return false;
                }
                records.emplace(key, std::move(record));
            }
        }
    }
    return true;
}

const LockedGitRecord* FindLockedGitEdge(
    const std::map<std::string, LockedGitRecord>& records,
    const PackageNode& package,
    const DependencyEdge& dependency)
{
    const std::string key = package.name + "\n" + package.version + "\n" +
        dependency.alias + "\n" + dependency.requestedVersion;
    const auto found = records.find(key);
    return found == records.end() ? nullptr : &found->second;
}

bool SameArtifact(const PackageArtifact& left, const PackageArtifact& right)
{
    return left.platform == right.platform &&
        left.architecture == right.architecture &&
        left.configuration == right.configuration &&
        left.abi == right.abi &&
        left.release == right.release &&
        left.asset == right.asset &&
        left.sha256 == right.sha256;
}

bool ReadFile(const fs::path& path, std::string& content)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

void RollbackMaterializedPackages(const fs::path& projectRoot,
                                  const std::vector<fs::path>& createdPackages)
{
    std::error_code ec;
    const fs::path modulesRoot = fs::weakly_canonical(projectRoot / "lode_modules", ec);
    if (ec)
        return;
    for (const fs::path& package : createdPackages)
    {
        const fs::path canonicalPackage = fs::weakly_canonical(package, ec);
        const fs::path relative = fs::relative(canonicalPackage, modulesRoot, ec);
        if (!ec && !relative.empty() && relative != "." &&
            *relative.begin() != "..")
        {
            fs::remove_all(canonicalPackage, ec);
        }
    }
}

bool ResolveGitDependencies(DependencyGraph& graph,
                             const fs::path& standardLibraryRoot,
                             const PackageCacheLayout& cacheLayout,
                             const fs::path& stagingDirectory,
                             const json* lockedDocument,
                             InstallResult& result)
{
    std::map<std::string, LockedGitRecord> lockedEdges;
    if (lockedDocument && !ParseLockedGitEdges(*lockedDocument, lockedEdges, result))
        return false;

    std::map<fs::path, size_t> packageIndexes;
    for (size_t index = 0; index < graph.packages.size(); ++index)
        packageIndexes.emplace(fs::weakly_canonical(graph.packages[index].root), index);

    std::map<std::string, size_t> resolvedRepositories;
    for (size_t packageIndex = 0; packageIndex < graph.packages.size(); ++packageIndex)
    {
        for (size_t edgeIndex = 0;
             edgeIndex < graph.packages[packageIndex].dependencies.size();
             ++edgeIndex)
        {
            if (graph.packages[packageIndex].dependencies[edgeIndex].source != DependencySource::Git ||
                graph.packages[packageIndex].dependencies[edgeIndex].target)
                continue;

            const DependencyEdge& dependency =
                graph.packages[packageIndex].dependencies[edgeIndex];
            const LockedGitRecord* locked = lockedDocument
                ? FindLockedGitEdge(lockedEdges, graph.packages[packageIndex], dependency)
                : nullptr;
            if (lockedDocument && !locked)
            {
                AddError(result, "lode.lock has no Git target for dependency '" +
                    dependency.alias + "'.");
                return false;
            }

            const std::string repository = locked
                ? locked->repository
                : dependency.sourceReference;
            size_t targetIndex = 0;
            const std::string resolutionKey = repository + "\n" +
                (locked ? locked->commit : dependency.requestedVersion);
            auto resolved = resolvedRepositories.find(resolutionKey);
            if (resolved != resolvedRepositories.end())
            {
                targetIndex = resolved->second;
            }
            else
            {
                std::string resolvedCommit;
                if (!locked)
                {
                    const GitTagResolutionResult tag = ResolveGitTag(
                        repository, dependency.requestedVersion);
                    if (!tag.IsValid())
                    {
                        result.errors.insert(result.errors.end(),
                            tag.errors.begin(), tag.errors.end());
                        return false;
                    }
                    resolvedCommit = tag.commit;
                }

                const GitCheckoutResult checkout = locked
                    ? CheckoutGitPackageAtCommit(repository, locked->commit, stagingDirectory)
                    : CheckoutGitPackageAtCommit(repository, resolvedCommit, stagingDirectory);
                if (!checkout.IsValid())
                {
                    result.errors.insert(result.errors.end(),
                        checkout.errors.begin(), checkout.errors.end());
                    return false;
                }

                const ValidationReport packageValidation = Validate(
                    checkout.packageRoot, ValidationMode::InstallSource, standardLibraryRoot);
                if (!packageValidation.IsValid())
                {
                    result.errors.insert(result.errors.end(),
                        packageValidation.errors.begin(), packageValidation.errors.end());
                    return false;
                }

                const DependencyGraph& resolvedGraph = packageValidation.dependencyGraph;
                if (resolvedGraph.root >= resolvedGraph.packages.size())
                {
                    AddError(result, "Git package '" + repository +
                        "' produced an invalid dependency graph.");
                    return false;
                }

                PackageNode resolvedRoot = resolvedGraph.packages[resolvedGraph.root];
                const DependencyEdge& edge = dependency;
                if (locked && (locked->repository != edge.sourceReference ||
                               locked->commit.empty()))
                {
                    AddError(result, "lode.lock Git source does not match dependency '" +
                        edge.alias + "'.");
                    return false;
                }
                if (!PackageVersionSatisfies(resolvedRoot.version, edge.requestedVersion))
                {
                    AddError(result, "Git dependency '" + edge.alias + "' requires version " +
                        edge.requestedVersion + ", but the repository contains " +
                        resolvedRoot.version + ".");
                    return false;
                }

                std::ifstream manifestFile(resolvedRoot.root / "lode.json");
                json manifest;
                if (!manifestFile.is_open() || !(manifestFile >> manifest))
                {
                    AddError(result, "Cannot inspect Git package manifest for '" +
                        resolvedRoot.name + "'.");
                    return false;
                }
                if (manifest.contains("libraries") && manifest["libraries"].is_object() &&
                    !manifest["libraries"].empty())
                {
                    const PackageArtifactResult artifact = DownloadGitHubPackageArtifact(
                        repository, resolvedRoot.name, resolvedRoot.version,
                        cacheLayout, stagingDirectory);
                    if (!artifact.IsValid())
                    {
                        result.errors.insert(result.errors.end(),
                            artifact.errors.begin(), artifact.errors.end());
                        return false;
                    }
                    if (locked)
                    {
                        const auto expectedArtifact = std::find_if(
                            locked->artifacts.begin(), locked->artifacts.end(),
                            [](const PackageArtifact& candidate) {
                                return candidate.platform == "windows" &&
                                    candidate.architecture == "x64" &&
                                    candidate.abi == LodeAbiId();
                            });
                        if (expectedArtifact == locked->artifacts.end() ||
                            !SameArtifact(*expectedArtifact, artifact.artifact))
                        {
                            AddError(result, "Downloaded GitHub artifact does not match the locked "
                                "platform, ABI, release, asset, or SHA-256.");
                            return false;
                        }
                    }

                    const ValidationReport artifactValidation = Validate(
                        artifact.packageRoot, ValidationMode::InstallArtifact, standardLibraryRoot);
                    if (!artifactValidation.IsValid())
                    {
                        result.errors.insert(result.errors.end(),
                            artifactValidation.errors.begin(), artifactValidation.errors.end());
                        return false;
                    }
                    if (artifactValidation.dependencyGraph.root >=
                        artifactValidation.dependencyGraph.packages.size())
                    {
                        AddError(result, "Downloaded artifact for '" + resolvedRoot.name +
                            "' produced an invalid dependency graph.");
                        return false;
                    }
                    const PackageNode& artifactRoot =
                        artifactValidation.dependencyGraph.packages[
                            artifactValidation.dependencyGraph.root];
                    if (artifactRoot.name != resolvedRoot.name ||
                        artifactRoot.version != resolvedRoot.version)
                    {
                        AddError(result, "Downloaded artifact identity does not match Git package '" +
                            resolvedRoot.name + "'.");
                        return false;
                    }

                    resolvedRoot.root = artifactRoot.root;
                    resolvedRoot.artifacts.push_back(artifact.artifact);
                }

                const fs::path canonicalRoot = fs::weakly_canonical(checkout.packageRoot);
                const fs::path packageRoot = fs::weakly_canonical(resolvedRoot.root);
                const std::vector<PackageArtifact> artifacts = resolvedRoot.artifacts;
                auto existingPackage = packageIndexes.find(canonicalRoot);
                if (existingPackage != packageIndexes.end())
                {
                    targetIndex = existingPackage->second;
                    PackageNode& existing = graph.packages[targetIndex];
                    existing.root = packageRoot;
                    existing.source = DependencySource::Git;
                    existing.sourceReference = repository;
                    existing.resolvedCommit = checkout.commit;
                    existing.artifacts = artifacts;
                }
                else
                {
                    targetIndex = graph.packages.size();
                    graph.packages.push_back(PackageNode{
                        resolvedRoot.name,
                        resolvedRoot.version,
                        packageRoot,
                        DependencySource::Git,
                        repository,
                        checkout.commit,
                        artifacts,
                        {}
                    });
                    packageIndexes.emplace(canonicalRoot, targetIndex);
                }

                std::vector<size_t> remapped(resolvedGraph.packages.size());
                remapped[resolvedGraph.root] = targetIndex;
                for (size_t resolvedIndex = 0;
                     resolvedIndex < resolvedGraph.packages.size();
                     ++resolvedIndex)
                {
                    if (resolvedIndex == resolvedGraph.root)
                        continue;

                    const PackageNode& resolvedPackage = resolvedGraph.packages[resolvedIndex];
                    const fs::path resolvedPath = fs::weakly_canonical(resolvedPackage.root);
                    auto package = packageIndexes.find(resolvedPath);
                    if (package != packageIndexes.end())
                    {
                        remapped[resolvedIndex] = package->second;
                        continue;
                    }

                    remapped[resolvedIndex] = graph.packages.size();
                    graph.packages.push_back(PackageNode{
                        resolvedPackage.name,
                        resolvedPackage.version,
                        resolvedPath,
                        resolvedPackage.source,
                        resolvedPackage.sourceReference,
                        resolvedPackage.resolvedCommit,
                        {}
                    });
                    packageIndexes.emplace(resolvedPath, remapped[resolvedIndex]);
                }

                for (size_t resolvedIndex = 0;
                     resolvedIndex < resolvedGraph.packages.size();
                     ++resolvedIndex)
                {
                    const PackageNode& resolvedPackage = resolvedGraph.packages[resolvedIndex];
                    PackageNode& destination = graph.packages[remapped[resolvedIndex]];
                    if (!destination.dependencies.empty())
                        continue;

                    for (const DependencyEdge& resolvedEdge : resolvedPackage.dependencies)
                    {
                        if (resolvedEdge.scope == DependencyScope::Development)
                            continue;

                        DependencyEdge edge = resolvedEdge;
                        if (edge.target)
                            edge.target = remapped[*edge.target];
                        destination.dependencies.push_back(std::move(edge));
                    }
                }

                resolvedRepositories.emplace(resolutionKey, targetIndex);
            }

            graph.packages[packageIndex].dependencies[edgeIndex].target = targetIndex;
        }
    }

    return true;
}

struct LockedStdlibRecord
{
    std::string name;
    std::string version;
    PackageArtifact artifact;
};

bool IsExactVersion(const std::string& value)
{
    return PackageVersionSatisfies(value, value);
}

std::optional<std::string> ReadStdlibRelease(const fs::path& standardLibraryRoot,
                                             InstallResult& result)
{
    if (standardLibraryRoot.empty())
    {
        AddError(result, "Cannot resolve an official standard-library artifact without the installed stdlib catalog.");
        return std::nullopt;
    }

    std::ifstream file(standardLibraryRoot / "VERSION");
    if (!file.is_open())
    {
        AddError(result, "The installed stdlib catalog has no VERSION release marker: " +
            PathToUtf8(standardLibraryRoot));
        return std::nullopt;
    }

    std::string release;
    std::getline(file, release);
    while (!release.empty() && std::isspace(static_cast<unsigned char>(release.back())))
        release.pop_back();
    if (release.empty() || release.find_first_of("/\\\"'") != std::string::npos)
    {
        AddError(result, "The installed stdlib VERSION marker is invalid: " +
            PathToUtf8(standardLibraryRoot / "VERSION"));
        return std::nullopt;
    }
    return release;
}

bool FindLockedStdlibRecord(const json& document,
                            const PackageNode& package,
                            const DependencyEdge& dependency,
                            LockedStdlibRecord& result,
                            InstallResult& installResult)
{
    if (!document.is_object() || !document.contains("packages") ||
        !document["packages"].is_array())
    {
        AddError(installResult, "Locked package installation requires a valid lode.lock packages array.");
        return false;
    }

    const std::string key = package.name + "\n" + package.version + "\n" +
        dependency.alias + "\n" + dependency.requestedVersion;
    const json& packages = document["packages"];
    for (const json& lockPackage : packages)
    {
        if (!lockPackage.is_object())
            continue;
        for (const char* field : { "dependencies", "devDependencies" })
        {
            if (!lockPackage.contains(field) || !lockPackage[field].is_array())
                continue;
            for (const json& lockDependency : lockPackage[field])
            {
                if (!lockDependency.is_object() ||
                    LockedEdgeKey(lockPackage, lockDependency) != key ||
                    !lockDependency.contains("target") ||
                    !lockDependency["target"].is_number_unsigned())
                    continue;

                const size_t target = lockDependency["target"].get<size_t>();
                if (target >= packages.size() || !packages[target].is_object() ||
                    packages[target].value("source", "") != "stdlib")
                    continue;

                const json& targetPackage = packages[target];
                result.name = targetPackage.value("name", "");
                result.version = targetPackage.value("version", "");
                if (result.name.empty() || result.version.empty() ||
                    !targetPackage.contains("artifacts") ||
                    !targetPackage["artifacts"].is_array())
                {
                    AddError(installResult, "Locked stdlib package record for '" +
                        dependency.alias + "' has no artifact record.");
                    return false;
                }

                for (const json& artifactDocument : targetPackage["artifacts"])
                {
                    if (!artifactDocument.is_object())
                        continue;
                    PackageArtifact artifact;
                    artifact.platform = artifactDocument.value("platform", "");
                    artifact.architecture = artifactDocument.value("architecture", "");
                    artifact.configuration = artifactDocument.value("configuration", "");
                    artifact.abi = artifactDocument.value("abi", "");
                    artifact.release = artifactDocument.value("release", "");
                    artifact.asset = artifactDocument.value("asset", "");
                    artifact.sha256 = artifactDocument.value("sha256", "");
                    if (artifact.platform == "windows" &&
                        artifact.architecture == "x64" &&
                        artifact.abi == LodeAbiId())
                    {
                        result.artifact = std::move(artifact);
                        return true;
                    }
                }

                AddError(installResult, "Locked stdlib package '" + dependency.alias +
                    "' has no Windows x64 artifact for ABI " + LodeAbiId() + ".");
                return false;
            }
        }
    }

    AddError(installResult, "lode.lock has no stdlib target for dependency '" +
        dependency.alias + "'.");
    return false;
}

bool MergeResolvedPackageGraph(DependencyGraph& graph,
                               std::map<fs::path, size_t>& packageIndexes,
                               const DependencyGraph& resolvedGraph,
                               const PackageNode& resolvedRoot,
                               size_t& targetIndex)
{
    if (resolvedGraph.root >= resolvedGraph.packages.size())
        return false;

    const fs::path canonicalRoot = fs::weakly_canonical(resolvedRoot.root);
    auto existingPackage = packageIndexes.find(canonicalRoot);
    if (existingPackage != packageIndexes.end())
    {
        targetIndex = existingPackage->second;
        graph.packages[targetIndex] = resolvedRoot;
    }
    else
    {
        targetIndex = graph.packages.size();
        graph.packages.push_back(resolvedRoot);
        packageIndexes.emplace(canonicalRoot, targetIndex);
    }

    std::vector<size_t> remapped(resolvedGraph.packages.size());
    remapped[resolvedGraph.root] = targetIndex;
    for (size_t resolvedIndex = 0;
         resolvedIndex < resolvedGraph.packages.size();
         ++resolvedIndex)
    {
        if (resolvedIndex == resolvedGraph.root)
            continue;

        const PackageNode& resolvedPackage = resolvedGraph.packages[resolvedIndex];
        const fs::path resolvedPath = fs::weakly_canonical(resolvedPackage.root);
        auto package = packageIndexes.find(resolvedPath);
        if (package != packageIndexes.end())
        {
            remapped[resolvedIndex] = package->second;
            continue;
        }

        remapped[resolvedIndex] = graph.packages.size();
        graph.packages.push_back(PackageNode{
            resolvedPackage.name,
            resolvedPackage.version,
            resolvedPath,
            resolvedPackage.source,
            resolvedPackage.sourceReference,
            resolvedPackage.resolvedCommit,
            resolvedPackage.artifacts,
            {}
        });
        packageIndexes.emplace(resolvedPath, remapped[resolvedIndex]);
    }

    for (size_t resolvedIndex = 0;
         resolvedIndex < resolvedGraph.packages.size();
         ++resolvedIndex)
    {
        const PackageNode& resolvedPackage = resolvedGraph.packages[resolvedIndex];
        PackageNode& destination = graph.packages[remapped[resolvedIndex]];
        if (!destination.dependencies.empty())
            continue;

        for (const DependencyEdge& resolvedEdge : resolvedPackage.dependencies)
        {
            if (resolvedEdge.scope == DependencyScope::Development)
                continue;

            DependencyEdge edge = resolvedEdge;
            if (edge.target)
                edge.target = remapped[*edge.target];
            destination.dependencies.push_back(std::move(edge));
        }
    }
    return true;
}

bool ResolveStandardLibraryDependencies(DependencyGraph& graph,
                                         const fs::path& standardLibraryRoot,
                                         const PackageCacheLayout& cacheLayout,
                                         const fs::path& stagingDirectory,
                                         const json* lockedDocument,
                                         InstallResult& result)
{
    constexpr const char* officialRepository = "github:lode-luau/LodeRuntime";
    bool needsResolution = false;
    for (const PackageNode& package : graph.packages)
    {
        for (const DependencyEdge& dependency : package.dependencies)
        {
            if (dependency.source == DependencySource::StandardLibrary && !dependency.target)
            {
                needsResolution = true;
                break;
            }
        }
        if (needsResolution)
            break;
    }
    if (!needsResolution)
        return true;

    std::map<fs::path, size_t> packageIndexes;
    std::map<std::string, size_t> resolvedPackages;
    for (size_t index = 0; index < graph.packages.size(); ++index)
        packageIndexes.emplace(fs::weakly_canonical(graph.packages[index].root), index);

    std::optional<std::string> release;
    if (!lockedDocument)
        release = ReadStdlibRelease(standardLibraryRoot, result);
    if (!lockedDocument && !release)
        return false;

    for (size_t packageIndex = 0; packageIndex < graph.packages.size(); ++packageIndex)
    {
        for (size_t edgeIndex = 0;
             edgeIndex < graph.packages[packageIndex].dependencies.size();
             ++edgeIndex)
        {
            DependencyEdge& dependency = graph.packages[packageIndex].dependencies[edgeIndex];
            if (dependency.source != DependencySource::StandardLibrary || dependency.target)
                continue;

            LockedStdlibRecord locked;
            std::string packageName = dependency.alias;
            std::string packageVersion = dependency.requestedVersion;
            std::string packageRelease;
            if (lockedDocument)
            {
                if (!FindLockedStdlibRecord(*lockedDocument,
                                             graph.packages[packageIndex],
                                             dependency,
                                             locked,
                                             result))
                    return false;
                packageName = locked.name;
                packageVersion = locked.version;
                packageRelease = locked.artifact.release;
                if (locked.artifact.asset != "lode-stdlib-" + packageName + "-" +
                    packageVersion + "-windows-x64.zip")
                {
                    AddError(result, "Locked stdlib artifact name does not match package '" +
                        packageName + "'.");
                    return false;
                }
            }
            else
            {
                if (!IsExactVersion(packageVersion))
                {
                    AddError(result, "Standard library dependency '" + dependency.alias +
                        "' requires an exact SemVer to select an official artifact.");
                    return false;
                }
                packageRelease = *release;
            }

            const std::string resolutionKey = packageName + "\n" + packageVersion +
                "\n" + packageRelease;
            auto resolved = resolvedPackages.find(resolutionKey);
            size_t targetIndex = 0;
            if (resolved != resolvedPackages.end())
            {
                targetIndex = resolved->second;
            }
            else
            {
                const PackageArtifactResult artifact = DownloadGitHubStdlibArtifact(
                    officialRepository,
                    packageName,
                    packageVersion,
                    packageRelease,
                    cacheLayout,
                    stagingDirectory);
                if (!artifact.IsValid())
                {
                    result.errors.insert(result.errors.end(),
                        artifact.errors.begin(), artifact.errors.end());
                    return false;
                }
                if (lockedDocument && !SameArtifact(locked.artifact, artifact.artifact))
                {
                    AddError(result, "Downloaded stdlib artifact does not match the locked "
                        "release, asset, ABI, or SHA-256.");
                    return false;
                }

                const ValidationReport validation = Validate(
                    artifact.packageRoot, ValidationMode::InstallArtifact, standardLibraryRoot);
                if (!validation.IsValid())
                {
                    result.errors.insert(result.errors.end(),
                        validation.errors.begin(), validation.errors.end());
                    return false;
                }
                if (validation.dependencyGraph.root >= validation.dependencyGraph.packages.size())
                {
                    AddError(result, "Downloaded stdlib artifact produced an invalid dependency graph.");
                    return false;
                }

                PackageNode resolvedRoot = validation.dependencyGraph.packages[
                    validation.dependencyGraph.root];
                if (resolvedRoot.name != packageName || resolvedRoot.version != packageVersion ||
                    !PackageVersionSatisfies(resolvedRoot.version, dependency.requestedVersion))
                {
                    AddError(result, "Downloaded stdlib artifact identity does not match dependency '" +
                        dependency.alias + "'.");
                    return false;
                }
                resolvedRoot.source = DependencySource::StandardLibrary;
                resolvedRoot.sourceReference.clear();
                resolvedRoot.resolvedCommit.clear();
                resolvedRoot.artifacts.push_back(artifact.artifact);

                if (!MergeResolvedPackageGraph(graph, packageIndexes,
                                                validation.dependencyGraph,
                                                resolvedRoot,
                                                targetIndex))
                {
                    AddError(result, "Failed to merge the dependency graph for stdlib package '" +
                        packageName + "'.");
                    return false;
                }
                resolvedPackages.emplace(resolutionKey, targetIndex);
            }

            dependency.target = targetIndex;
        }
    }

    return true;
}

struct LockedGraphResult
{
    DependencyGraph graph;
    fs::path stagingDirectory;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty();
    }
};

LockedGraphResult ResolveLockedGraph(const fs::path& packageRoot,
                                     const fs::path& standardLibraryRoot,
                                     const PackageCacheLayout& cacheLayout,
                                     bool includeDevelopmentDependencies)
{
    LockedGraphResult result;
    const ValidationReport validation = Validate(
        packageRoot, ValidationMode::InstallSource, standardLibraryRoot);
    if (!validation.IsValid())
    {
        result.errors = validation.errors;
        return result;
    }

    result.graph = validation.dependencyGraph;
    const fs::path root = result.graph.packages[result.graph.root].root;
    const std::vector<bool> initialInstallablePackages = CollectInstallablePackages(
        result.graph, includeDevelopmentDependencies);
    if (!HasInstallEdges(result.graph, initialInstallablePackages,
                         includeDevelopmentDependencies))
        return result;

    std::string lockContent;
    if (!ReadFile(root / "lode.lock", lockContent))
    {
        result.errors.push_back("Cannot open lockfile: " + PathToUtf8(root / "lode.lock"));
        return result;
    }

    json lockDocument;
    try
    {
        lockDocument = json::parse(lockContent);
    }
    catch (const std::exception& error)
    {
        result.errors.push_back("Failed to parse lockfile " +
            PathToUtf8(root / "lode.lock") + ": " + error.what());
        return result;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path stagingDirectory = cacheLayout.stagingDirectory /
        ("git-locked-" + std::to_string(timestamp));
    InstallResult resolution;
    auto cleanup = [&]() {
        std::error_code ec;
        fs::remove_all(stagingDirectory, ec);
    };

    if (!ResolveGitDependencies(result.graph, standardLibraryRoot, cacheLayout,
                                stagingDirectory, &lockDocument, resolution))
    {
        cleanup();
        result.errors = std::move(resolution.errors);
        return result;
    }

    if (!ResolveStandardLibraryDependencies(result.graph, standardLibraryRoot, cacheLayout,
                                            stagingDirectory, &lockDocument, resolution))
    {
        cleanup();
        result.errors = std::move(resolution.errors);
        return result;
    }

    const LockfileResult lock = ValidateLockfile(root / "lode.lock", result.graph);
    if (!lock.IsValid())
    {
        cleanup();
        result.errors = lock.errors;
        return result;
    }

    result.stagingDirectory = stagingDirectory;
    return result;
}

} // namespace

InstallResult InstallResolvedGraph(const DependencyGraph& graph,
                                   const fs::path& root,
                                   const PackageCacheLayout& cacheLayout,
                                   bool includeDevelopmentDependencies)
{
    InstallResult result;
    const std::vector<bool> installablePackages = CollectInstallablePackages(
        graph, includeDevelopmentDependencies);
    if (!HasInstallEdges(graph, installablePackages, includeDevelopmentDependencies))
        return result;

    std::map<std::string, PackageCacheIdentity> aliases;
    std::map<size_t, PackageCacheIdentity> packages;
    for (size_t packageIndex = 0; packageIndex < graph.packages.size(); ++packageIndex)
    {
        for (const DependencyEdge& edge : graph.packages[packageIndex].dependencies)
        {
            if (!IsInstallEdge(graph, packageIndex, edge, installablePackages,
                               includeDevelopmentDependencies))
                continue;
            if (!edge.target)
            {
                AddError(result, "Cannot install dependency '" + edge.alias +
                    "': its " + (edge.source == DependencySource::Git ? "Git" : "package") +
                    " resolution is incomplete.");
                continue;
            }

            const size_t target = *edge.target;
            auto package = packages.find(target);
            if (package == packages.end())
            {
                const CacheIdentityResult identity = ResolvePackageCacheIdentity(
                    graph, target, cacheLayout);
                if (!identity.IsValid())
                {
                    result.errors.insert(result.errors.end(),
                        identity.errors.begin(), identity.errors.end());
                    continue;
                }
                package = packages.emplace(target, *identity.identity).first;
            }

            const auto existing = aliases.find(edge.alias);
            if (existing != aliases.end() &&
                existing->second.canonicalDocument != package->second.canonicalDocument)
            {
                AddError(result, "Cannot install: alias '" + edge.alias +
                    "' resolves to multiple package identities.");
                continue;
            }
            aliases.emplace(edge.alias, package->second);
        }
    }

    if (!result.IsValid())
        return result;

    for (const auto& [packageIndex, identity] : packages)
    {
        const CachePopulationResult population = PopulatePackageCache(
            cacheLayout, identity, graph.packages[packageIndex].root);
        if (!population.IsValid())
        {
            result.errors.insert(result.errors.end(),
                population.errors.begin(), population.errors.end());
            return result;
        }
    }

    std::vector<fs::path> createdPackages;
    std::vector<std::pair<std::string, std::string>> configAliases;
    for (const auto& [alias, identity] : aliases)
    {
        const MaterializationResult materialization = MaterializePackage(
            cacheLayout, identity, root, alias);
        if (!materialization.IsValid())
        {
            result.errors.insert(result.errors.end(),
                materialization.errors.begin(), materialization.errors.end());
            RollbackMaterializedPackages(root, createdPackages);
            return result;
        }
        result.materializedPackages.push_back(materialization.packageDirectory);
        configAliases.emplace_back(alias, "lode_modules/" + alias);
        if (!materialization.reused)
            createdPackages.push_back(materialization.packageDirectory);
    }

    if (!configAliases.empty())
    {
        const fs::path configPath = root / ".config.luau";
        ConfigAliasUpdateResult configResult;
        std::error_code ec;
        if (fs::is_regular_file(configPath, ec))
        {
            std::string content;
            if (!ReadFile(configPath, content))
            {
                AddError(result, "Cannot read .config.luau: " + PathToUtf8(configPath));
                RollbackMaterializedPackages(root, createdPackages);
                return result;
            }
            configResult = UpdateConfigAliases(content, configAliases);
            if (configResult.IsValid() && configResult.changed)
            {
                configResult = WriteConfigAliases(configPath, configAliases);
            }
        }
        else if (!fs::exists(configPath, ec))
        {
            configResult = WriteGeneratedConfigAliases(configPath, configAliases);
        }
        else
        {
            AddError(result, ".config.luau is not a regular file: " + PathToUtf8(configPath));
            RollbackMaterializedPackages(root, createdPackages);
            return result;
        }

        if (!configResult.IsValid())
        {
            result.errors.insert(result.errors.end(),
                configResult.errors.begin(), configResult.errors.end());
            RollbackMaterializedPackages(root, createdPackages);
            return result;
        }
    }

    return result;
}

InstallResult InstallLocked(const fs::path& packageRoot,
                            const fs::path& standardLibraryRoot,
                            bool includeDevelopmentDependencies)
{
    InstallResult result;

    const CacheLayoutResult cache = ResolvePackageCacheLayout();
    if (!cache.IsValid())
    {
        result.errors = cache.errors;
        return result;
    }

    LockedGraphResult resolution = ResolveLockedGraph(
        packageRoot, standardLibraryRoot, *cache.layout, includeDevelopmentDependencies);
    if (!resolution.IsValid())
    {
        result.errors = std::move(resolution.errors);
        return result;
    }

    const fs::path root = resolution.graph.packages[resolution.graph.root].root;
    result = InstallResolvedGraph(resolution.graph, root, *cache.layout,
                                  includeDevelopmentDependencies);
    std::error_code ec;
    if (!resolution.stagingDirectory.empty())
        fs::remove_all(resolution.stagingDirectory, ec);
    return result;
}

ValidationReport ValidateLockedPackage(const fs::path& packageRoot,
                                       const fs::path& standardLibraryRoot,
                                       bool includeDevelopmentDependencies)
{
    ValidationReport report;
    const CacheLayoutResult cache = ResolvePackageCacheLayout();
    if (!cache.IsValid())
    {
        report.errors = cache.errors;
        return report;
    }

    LockedGraphResult resolution = ResolveLockedGraph(
        packageRoot, standardLibraryRoot, *cache.layout, includeDevelopmentDependencies);
    report.errors = std::move(resolution.errors);
    report.dependencyGraph = std::move(resolution.graph);
    std::error_code ec;
    if (!resolution.stagingDirectory.empty())
        fs::remove_all(resolution.stagingDirectory, ec);
    return report;
}

InstallResult InstallLocal(const fs::path& packageRoot,
                           const fs::path& standardLibraryRoot,
                           bool includeDevelopmentDependencies)
{
    InstallResult result;
    const ValidationReport validation = Validate(
        packageRoot, ValidationMode::InstallSource, standardLibraryRoot);
    if (!validation.IsValid())
    {
        result.errors = validation.errors;
        return result;
    }

    DependencyGraph graph = validation.dependencyGraph;
    const fs::path root = graph.packages[graph.root].root;

    const CacheLayoutResult cache = ResolvePackageCacheLayout();
    if (!cache.IsValid())
    {
        result.errors = cache.errors;
        return result;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path gitStaging = cache.layout->stagingDirectory /
        ("git-" + std::to_string(timestamp));
    if (!ResolveGitDependencies(graph, standardLibraryRoot, *cache.layout,
                                gitStaging, nullptr, result))
    {
        std::error_code ec;
        fs::remove_all(gitStaging, ec);
        return result;
    }

    if (!ResolveStandardLibraryDependencies(graph, standardLibraryRoot, *cache.layout,
                                            gitStaging, nullptr, result))
    {
        std::error_code ec;
        fs::remove_all(gitStaging, ec);
        return result;
    }

    const LockfileResult lock = BuildLockfile(graph);
    if (!lock.IsValid())
    {
        result.errors = lock.errors;
        std::error_code ec;
        fs::remove_all(gitStaging, ec);
        return result;
    }

    result = InstallResolvedGraph(graph, root, *cache.layout,
                                  includeDevelopmentDependencies);
    if (!result.IsValid())
    {
        std::error_code ec;
        fs::remove_all(gitStaging, ec);
        return result;
    }

    const LockfileResult written = WriteLockfile(root / "lode.lock", graph);
    if (!written.IsValid())
        result.errors = written.errors;
    std::error_code ec;
    fs::remove_all(gitStaging, ec);
    return result;
}

} // namespace Lode::Package
