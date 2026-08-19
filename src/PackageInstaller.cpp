// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageInstaller.hpp"

#include "PackageCache.hpp"
#include "PackageConfig.hpp"
#include "PackageLockfile.hpp"
#include "PackageValidator.hpp"
#include "PathUtil.hpp"

#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathToUtf8;

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

} // namespace

InstallResult InstallLocked(const fs::path& packageRoot,
                            const fs::path& standardLibraryRoot,
                            bool includeDevelopmentDependencies)
{
    InstallResult result;
    const ValidationReport validation = Validate(
        packageRoot, ValidationMode::Source, standardLibraryRoot);
    if (!validation.IsValid())
    {
        result.errors = validation.errors;
        return result;
    }

    const DependencyGraph& graph = validation.dependencyGraph;
    const fs::path root = graph.packages[graph.root].root;
    const fs::path lockfilePath = root / "lode.lock";
    const std::vector<bool> installablePackages = CollectInstallablePackages(
        graph, includeDevelopmentDependencies);
    if (HasInstallEdges(graph, installablePackages, includeDevelopmentDependencies))
    {
        const LockfileResult lock = ValidateLockfile(lockfilePath, graph);
        if (!lock.IsValid())
        {
            result.errors = lock.errors;
            return result;
        }
    }

    if (!HasInstallEdges(graph, installablePackages, includeDevelopmentDependencies))
        return result;

    const CacheLayoutResult cache = ResolvePackageCacheLayout();
    if (!cache.IsValid())
    {
        result.errors = cache.errors;
        return result;
    }

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
                    graph, target, *cache.layout);
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
            *cache.layout, identity, graph.packages[packageIndex].root);
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
            *cache.layout, identity, root, alias);
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

} // namespace Lode::Package
