// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TtyExport.hpp"
#include "Lode/Table.hpp"
#include <memory>
#include <vector>

struct lua_State;
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace lodetty
{

struct TtyStream;

struct TTY_API TtyManager : std::enable_shared_from_this<TtyManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table streamMethods;

    std::vector<std::shared_ptr<TtyStream>> streams;

    void AddStream(const std::shared_ptr<TtyStream>& stream);
    void RemoveStream(const std::shared_ptr<TtyStream>& stream);

    void Shutdown();
};

} // namespace lodetty