// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "WebSocket/WebSocketManager.hpp"
#include "WebSocket/WsClient.hpp"
#include "WebSocket/WsServer.hpp"
#include <algorithm>

namespace lodews
{

void WebSocketManager::AddClient(const std::shared_ptr<WsClient>& client)
{
    clients.push_back(client);
}

void WebSocketManager::AddServer(const std::shared_ptr<WsServer>& server)
{
    servers.push_back(server);
}

void WebSocketManager::RemoveClient(const std::shared_ptr<WsClient>& client)
{
    auto it = std::find(clients.begin(), clients.end(), client);
    if (it != clients.end())
        clients.erase(it);
}

void WebSocketManager::RemoveServer(const std::shared_ptr<WsServer>& server)
{
    auto it = std::find(servers.begin(), servers.end(), server);
    if (it != servers.end())
        servers.erase(it);
}

void WebSocketManager::Shutdown()
{
    if (shuttingDown)
        return;
    shuttingDown = true;
    auto s = servers;
    for (auto& server : s)
        server->RequestClose();
    auto c = clients;
    for (auto& client : c)
        client->RequestClose();
}

} // namespace lodews
