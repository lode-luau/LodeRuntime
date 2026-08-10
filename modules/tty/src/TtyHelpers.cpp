// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tty/TtyHelpers.hpp"
#include "uv.h"

namespace lodetty
{

bool IsTtyFd(int fd)
{
    return uv_guess_handle(static_cast<uv_file>(fd)) == UV_TTY;
}

} // namespace lodetty