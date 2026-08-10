// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Table.hpp"
#include <memory>
#include <vector>

struct lua_State;
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace lodehttp
{

struct HttpClient;
struct HttpServer;

struct HttpManager : std::enable_shared_from_this<HttpManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table clientMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<HttpClient>> clients;
    std::vector<std::shared_ptr<HttpServer>> servers;

    void AddClient(const std::shared_ptr<HttpClient>& client);
    void AddServer(const std::shared_ptr<HttpServer>& server);
    void RemoveClient(const std::shared_ptr<HttpClient>& client);
    void RemoveServer(const std::shared_ptr<HttpServer>& server);
    
    void Shutdown();
};

} // namespace lodehttp
