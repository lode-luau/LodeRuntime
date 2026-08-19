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

struct DependencyEdge
{
    std::string alias;
    DependencySource source = DependencySource::Path;
    std::string requestedVersion;
    std::string sourceReference;
    std::optional<size_t> target;
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

} // namespace Lode::Package
