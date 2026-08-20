// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageLockfile.hpp"

#include "PathUtil.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using json = nlohmann::json;
using Lode::Detail::PathToUtf8;

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

int SourceRank(DependencySource source)
{
    switch (source)
    {
        case DependencySource::Root: return 0;
        case DependencySource::StandardLibrary: return 1;
        case DependencySource::Path: return 2;
        case DependencySource::Git: return 3;
    }
    return 4;
}

std::string RelativePackageReference(const DependencyGraph& graph, const PackageNode& package)
{
    if (package.source != DependencySource::Path)
        return {};

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(graph.packages[graph.root].root, ec);
    if (ec)
        return {};

    const fs::path relative = fs::relative(package.root, root, ec);
    if (ec || relative.empty() || relative == "." || relative.is_absolute())
        return {};

    return relative.generic_string();
}

std::string PackageReference(const DependencyGraph& graph, const PackageNode& package)
{
    if (package.source == DependencySource::Git)
        return package.sourceReference;
    if (package.source == DependencySource::Path)
        return RelativePackageReference(graph, package);
    return {};
}

bool PackageLess(const DependencyGraph& graph, size_t left, size_t right)
{
    const PackageNode& a = graph.packages[left];
    const PackageNode& b = graph.packages[right];

    if (SourceRank(a.source) != SourceRank(b.source))
        return SourceRank(a.source) < SourceRank(b.source);
    if (a.name != b.name)
        return a.name < b.name;
    if (a.version != b.version)
        return a.version < b.version;

    const std::string aReference = PackageReference(graph, a);
    const std::string bReference = PackageReference(graph, b);
    if (aReference != bReference)
        return aReference < bReference;

    if (a.resolvedCommit != b.resolvedCommit)
        return a.resolvedCommit < b.resolvedCommit;

    return false;
}

void AddError(LockfileResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool ReplaceFileAtomically(const fs::path& temporaryPath,
                           const fs::path& destinationPath,
                           std::string& error)
{
#if defined(_WIN32)
    if (!MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "atomic replacement failed with Windows error " +
            std::to_string(GetLastError());
        return false;
    }
    return true;
#else
    std::error_code ec;
    fs::rename(temporaryPath, destinationPath, ec);
    if (ec)
    {
        error = "atomic replacement failed: " + ec.message();
        return false;
    }
    return true;
#endif
}

} // namespace

LockfileResult BuildLockfile(const DependencyGraph& graph)
{
    LockfileResult result;

    if (graph.packages.empty())
    {
        AddError(result, "Cannot build lode.lock from an empty dependency graph.");
        return result;
    }

    if (graph.root >= graph.packages.size())
    {
        AddError(result, "Cannot build lode.lock: the graph root index is invalid.");
        return result;
    }

    std::vector<size_t> ordered;
    ordered.reserve(graph.packages.size());
    ordered.push_back(graph.root);
    for (size_t index = 0; index < graph.packages.size(); ++index)
    {
        if (index != graph.root)
            ordered.push_back(index);
    }
    std::sort(ordered.begin() + 1, ordered.end(), [&graph](size_t left, size_t right) {
        return PackageLess(graph, left, right);
    });

    std::map<size_t, size_t> remappedIndexes;
    for (size_t newIndex = 0; newIndex < ordered.size(); ++newIndex)
        remappedIndexes.emplace(ordered[newIndex], newIndex);

    json document = {
        { "lockfileVersion", 1 },
        { "root", 0 },
        { "packages", json::array() }
    };

    for (const size_t oldIndex : ordered)
    {
        const PackageNode& package = graph.packages[oldIndex];
        json packageDocument = {
            { "name", package.name },
            { "version", package.version },
            { "source", SourceName(package.source) },
            { "dependencies", json::array() }
        };

        if (package.source == DependencySource::Git)
        {
            if (package.sourceReference.empty())
                AddError(result, "Cannot write lode.lock: Git package '" + package.name +
                    "' has no repository reference.");
            if (package.resolvedCommit.empty())
                AddError(result, "Cannot write lode.lock: Git package '" + package.name +
                    "' has no resolved commit.");
            if (!package.sourceReference.empty())
                packageDocument["reference"] = package.sourceReference;
            if (!package.resolvedCommit.empty())
                packageDocument["commit"] = package.resolvedCommit;
        }
        else if (package.source == DependencySource::Path)
        {
            const std::string reference = RelativePackageReference(graph, package);
            if (reference.empty())
            {
                AddError(result, "Cannot write lode.lock: path package '" + package.name +
                    "' has no project-relative source reference.");
            }
            else
            {
                packageDocument["reference"] = reference;
            }
        }

        if (package.artifact)
        {
            packageDocument["artifact"] = {
                { "platform", package.artifact->platform },
                { "architecture", package.artifact->architecture },
                { "abi", package.artifact->abi },
                { "release", package.artifact->release },
                { "asset", package.artifact->asset },
                { "sha256", package.artifact->sha256 }
            };
        }

        std::vector<const DependencyEdge*> dependencies;
        dependencies.reserve(package.dependencies.size());
        for (const DependencyEdge& dependency : package.dependencies)
            dependencies.push_back(&dependency);
        std::sort(dependencies.begin(), dependencies.end(), [](const DependencyEdge* left, const DependencyEdge* right) {
            return left->alias < right->alias;
        });

        for (const DependencyEdge* dependency : dependencies)
        {
            if (!dependency->target)
            {
                const std::string source = SourceName(dependency->source);
                AddError(result, "Cannot write lode.lock: dependency '" + dependency->alias +
                    "' has unresolved " + source + " resolution.");
                continue;
            }

            const auto target = remappedIndexes.find(*dependency->target);
            if (target == remappedIndexes.end())
            {
                AddError(result, "Cannot write lode.lock: dependency '" + dependency->alias +
                    "' points to an invalid graph node.");
                continue;
            }

            if (dependency->scope == DependencyScope::Development)
            {
                if (oldIndex != graph.root)
                {
                    AddError(result, "Cannot write lode.lock: development dependency '" +
                        dependency->alias + "' belongs to a non-root package.");
                    continue;
                }
                if (!packageDocument.contains("devDependencies"))
                    packageDocument["devDependencies"] = json::array();
            }

            const char* fieldName = dependency->scope == DependencyScope::Development
                ? "devDependencies"
                : "dependencies";
            packageDocument[fieldName].push_back({
                { "alias", dependency->alias },
                { "requirement", dependency->requestedVersion },
                { "target", target->second }
            });
        }

        document["packages"].push_back(std::move(packageDocument));
    }

    if (result.IsValid())
        result.content = document.dump(2) + "\n";
    return result;
}

LockfileResult WriteLockfile(const fs::path& lockfilePath, const DependencyGraph& graph)
{
    LockfileResult result = BuildLockfile(graph);
    if (!result.IsValid())
        return result;

    std::error_code ec;
    const fs::path absoluteLockfile = fs::absolute(lockfilePath, ec);
    if (ec)
    {
        AddError(result, "Cannot determine the lockfile path: " + ec.message());
        return result;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporaryPath = absoluteLockfile;
    temporaryPath += ".tmp-" + std::to_string(timestamp);

    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            AddError(result, "Cannot open temporary lockfile for writing: " + PathToUtf8(temporaryPath));
            return result;
        }

        file.write(result.content.data(), static_cast<std::streamsize>(result.content.size()));
        file.flush();
        if (!file.good())
        {
            AddError(result, "Failed to write temporary lockfile: " + PathToUtf8(temporaryPath));
            file.close();
            fs::remove(temporaryPath, ec);
            return result;
        }
    }

    std::string replacementError;
    if (!ReplaceFileAtomically(temporaryPath, absoluteLockfile, replacementError))
    {
        AddError(result, "Cannot replace lockfile '" + PathToUtf8(absoluteLockfile) + "': " + replacementError);
        fs::remove(temporaryPath, ec);
        return result;
    }

    return result;
}

LockfileResult ValidateLockfile(const fs::path& lockfilePath, const DependencyGraph& graph)
{
    LockfileResult expected = BuildLockfile(graph);
    if (!expected.IsValid())
        return expected;

    std::ifstream file(lockfilePath, std::ios::binary);
    if (!file.is_open())
    {
        AddError(expected, "Cannot open lockfile: " + PathToUtf8(lockfilePath));
        return expected;
    }

    const std::istreambuf_iterator<char> begin(file);
    const std::istreambuf_iterator<char> end;
    const std::string content(begin, end);

    try
    {
        const json actual = json::parse(content);
        const json expectedDocument = json::parse(expected.content);
        if (actual != expectedDocument)
        {
            expected.errors.push_back("Lockfile does not match the current dependency graph: " +
                PathToUtf8(lockfilePath));
            expected.content.clear();
            return expected;
        }
    }
    catch (const std::exception& exception)
    {
        expected.errors.push_back("Failed to parse lockfile " + PathToUtf8(lockfilePath) + ": " + exception.what());
        expected.content.clear();
        return expected;
    }

    expected.content = content;
    return expected;
}

} // namespace Lode::Package
