// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "PackageGraph.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct LockfileResult
{
    std::string content;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty();
    }
};

// Builds the versioned lockfile document for a graph that has already been
// validated and resolved. It records selected GitHub artifacts when present.
LODE_API LockfileResult BuildLockfile(const DependencyGraph& graph);

// Writes a validated lockfile through a temporary file and an atomic replace.
// The target is not changed when graph serialization or the write fails.
LODE_API LockfileResult WriteLockfile(const std::filesystem::path& lockfilePath,
                                      const DependencyGraph& graph);

// Verifies that an existing lockfile is valid JSON and describes exactly the
// current validated graph. This never modifies the lockfile.
LODE_API LockfileResult ValidateLockfile(const std::filesystem::path& lockfilePath,
                                         const DependencyGraph& graph);

} // namespace Lode::Package
