// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "StdioExport.hpp"
#include <string>
#include <memory>
#include <vector>
#include "Lode/State.hpp"
#include "Lode/Table.hpp"

struct lua_State;
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace lodestdio
{

struct StdioStream;

struct STDIO_API StdioManager : std::enable_shared_from_this<StdioManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table streamMethods;

    std::shared_ptr<StdioStream> stdinStream;
    std::shared_ptr<StdioStream> stdoutStream;
    std::shared_ptr<StdioStream> stderrStream;

    std::vector<std::shared_ptr<StdioStream>> streams;

    void AddStream(const std::shared_ptr<StdioStream>& stream);
    void RemoveStream(const std::shared_ptr<StdioStream>& stream);

    void Shutdown();
};

} // namespace lodestdio
