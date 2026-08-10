// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TtyExport.hpp"

namespace lodetty
{

// Returns true if the given file descriptor is a TTY (uv_guess_handle == UV_TTY).
TTY_API bool IsTtyFd(int fd);

} // namespace lodetty