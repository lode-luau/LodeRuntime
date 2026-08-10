// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PipeExport.hpp"
#include "Lode/Table.hpp"
#include <memory>
#include <vector>

struct lua_State;
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace lodepipe
{

struct PipeStream;
struct PipeServer;

struct PIPE_API PipeManager : std::enable_shared_from_this<PipeManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table streamMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<PipeStream>> streams;
    std::vector<std::shared_ptr<PipeServer>> servers;

    void AddStream(const std::shared_ptr<PipeStream>& stream);
    void AddServer(const std::shared_ptr<PipeServer>& server);
    void RemoveStream(const std::shared_ptr<PipeStream>& stream);
    void RemoveServer(const std::shared_ptr<PipeServer>& server);

    void Shutdown();
};

} // namespace lodepipe