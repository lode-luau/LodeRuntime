// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PipeExport.hpp"
#include <string>

namespace lodepipe
{

PIPE_API std::string NormalizePipePath(const std::string& path);
PIPE_API bool IsPipeFd(int fd);

} // namespace lodepipe