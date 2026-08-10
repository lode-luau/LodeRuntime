// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "WebSocketExport.hpp"
#include "WsHelpers.hpp"
#include "Lode/Table.hpp"
#include "uv.h"
#include <memory>
#include <vector>

namespace lodews
{

struct WsClient;
struct WsServer;

struct WEBSOCKET_API WebSocketManager : std::enable_shared_from_this<WebSocketManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table clientMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<WsClient>> clients;
    std::vector<std::shared_ptr<WsServer>> servers;

    WsRng rng;

    void AddClient(const std::shared_ptr<WsClient>& client);
    void AddServer(const std::shared_ptr<WsServer>& server);
    void RemoveClient(const std::shared_ptr<WsClient>& client);
    void RemoveServer(const std::shared_ptr<WsServer>& server);
    void Shutdown();
};

} // namespace lodews
