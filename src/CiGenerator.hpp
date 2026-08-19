// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PackageValidator.hpp"

#include <filesystem>

namespace Lode::Package
{

LODE_API ValidationReport GenerateWorkflow(const std::filesystem::path& packageRoot, bool force);

} // namespace Lode::Package
