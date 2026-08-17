// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Stdio/StdioStream.hpp"
#include "Stdio/StdioManager.hpp"
#include "Stdio/StdioHelpers.hpp"
#include <uv.h>
#include <vector>
#include <string>

namespace lodestdio
{

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
    StdioStream* stream = nullptr;
};

struct FileWriteRequest
{
    uv_fs_t req;
    uv_buf_t buf;
    StdioStream* stream = nullptr;
};

struct FileReadRequest
{
    uv_fs_t req;
    uv_buf_t buf;
    StdioStream* stream = nullptr;
};

} // namespace lodestdio
