// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Stdio/StdioHelpers.hpp"
#include <uv.h>

namespace lodestdio
{

bool IsTtyFd(int fd)
{
    return uv_guess_handle(static_cast<uv_file>(fd)) == UV_TTY;
}

} // namespace lodestdio
