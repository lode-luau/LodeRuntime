// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include "Lode/Table.hpp"
#include <memory>
#include <vector>

struct lua_State;
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace lodetcp
{

struct TcpClient;
struct TcpServer;

struct TCP_API TcpManager : std::enable_shared_from_this<TcpManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table clientMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<TcpClient>> clients;
    std::vector<std::shared_ptr<TcpServer>> servers;

    void AddClient(const std::shared_ptr<TcpClient>& client);
    void AddServer(const std::shared_ptr<TcpServer>& server);
    void RemoveClient(const std::shared_ptr<TcpClient>& client);
    void RemoveServer(const std::shared_ptr<TcpServer>& server);
    
    void Shutdown();
};

} // namespace lodetcp
