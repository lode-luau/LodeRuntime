// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Lode::Package
{

struct CompiledImplementation
{
    std::string artifact;
    std::string layout;
    bool required = false;
};

struct PackageManifest
{
    std::string name;
    std::string version;
    bool hasImplementation = false;
    CompiledImplementation implementation;
};

struct PackageManifestResult
{
    PackageManifest manifest;
    std::vector<std::string> errors;

    [[nodiscard]] bool IsValid() const { return errors.empty(); }
};

// Reads the statically representable package.luau manifest without executing
// package code. The initial runtime reader consumes package identity and the
// optional compiled implementation contract; dependency fields are skipped
// here and remain owned by the package manager.
LODE_API PackageManifestResult ReadPackageManifest(
    const std::filesystem::path& manifestPath);

} // namespace Lode::Package
