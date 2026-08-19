// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Lode::Package
{

enum class DependencySource
{
    Root,
    StandardLibrary,
    Path,
    Git,
};

enum class DependencyScope
{
    Runtime,
    Development,
};

struct DependencyEdge
{
    std::string alias;
    DependencySource source = DependencySource::Path;
    std::string requestedVersion;
    std::string sourceReference;
    std::optional<size_t> target;
    DependencyScope scope = DependencyScope::Runtime;
};

struct PackageNode
{
    std::string name;
    std::string version;
    std::filesystem::path root;
    DependencySource source = DependencySource::Path;
    std::vector<DependencyEdge> dependencies;
};

struct DependencyGraph
{
    size_t root = 0;
    std::vector<PackageNode> packages;
};

// Produces a deterministic, portable snapshot of the validated graph. This
// is an in-memory candidate for the lockfile; it does not write to disk and it
// deliberately excludes machine-specific absolute package roots.
std::string SerializeDependencyGraph(const DependencyGraph& graph);

} // namespace Lode::Package
