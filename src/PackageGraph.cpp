// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "PackageGraph.hpp"

#include "nlohmann/json.hpp"

#include <utility>

namespace Lode::Package
{

namespace
{

using json = nlohmann::json;

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

} // namespace

std::string SerializeDependencyGraph(const DependencyGraph& graph)
{
    json document;
    document["root"] = graph.root;
    document["packages"] = json::array();

    for (const PackageNode& package : graph.packages)
    {
        json packageDocument = {
            { "name", package.name },
            { "version", package.version },
            { "source", SourceName(package.source) },
            { "dependencies", json::array() }
        };

        for (const DependencyEdge& dependency : package.dependencies)
        {
            json dependencyDocument = {
                { "alias", dependency.alias },
                { "source", SourceName(dependency.source) },
                { "version", dependency.requestedVersion }
            };

            if (!dependency.sourceReference.empty())
                dependencyDocument["reference"] = dependency.sourceReference;
            if (dependency.target)
                dependencyDocument["target"] = *dependency.target;

            const char* fieldName = dependency.scope == DependencyScope::Development
                ? "devDependencies"
                : "dependencies";
            if (!packageDocument.contains(fieldName))
                packageDocument[fieldName] = json::array();
            packageDocument[fieldName].push_back(std::move(dependencyDocument));
        }

        document["packages"].push_back(std::move(packageDocument));
    }

    return document.dump(2) + "\n";
}

} // namespace Lode::Package
