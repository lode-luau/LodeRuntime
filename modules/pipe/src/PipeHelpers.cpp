// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Pipe/PipeHelpers.hpp"
#include "uv.h"

namespace lodepipe
{

// On Windows, named pipes are addressed as \\.\pipe\name. libuv's
// uv_pipe_bind/uv_pipe_connect accept the full path; if the caller passes a
// bare name we normalize it to the canonical form. On POSIX the path is used
// as-is (a filesystem path to a FIFO/socket).
std::string NormalizePipePath(const std::string& path)
{
#ifdef _WIN32
    if (path.rfind("\\\\.\\pipe\\", 0) == 0)
        return path;
    if (path.rfind("//./pipe/", 0) == 0)
        return path;
    return "\\\\.\\pipe\\" + path;
#else
    (void)path;
    return path;
#endif
}

bool IsPipeFd(int fd)
{
    return uv_guess_handle(static_cast<uv_file>(fd)) == UV_NAMED_PIPE;
}

} // namespace lodepipe