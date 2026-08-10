// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tcp/TcpManager.hpp"
#include "Tcp/TcpClient.hpp"
#include "Tcp/TcpServer.hpp"
#include <algorithm>

namespace lodetcp
{

void TcpManager::AddClient(const std::shared_ptr<TcpClient>& client) 
{ 
    clients.push_back(client); 
}

void TcpManager::AddServer(const std::shared_ptr<TcpServer>& server) 
{ 
    servers.push_back(server); 
}

void TcpManager::RemoveClient(const std::shared_ptr<TcpClient>& client)
{
    auto it = std::find(clients.begin(), clients.end(), client);
    if (it != clients.end())
        clients.erase(it);
}

void TcpManager::RemoveServer(const std::shared_ptr<TcpServer>& server)
{
    auto it = std::find(servers.begin(), servers.end(), server);
    if (it != servers.end())
        servers.erase(it);
}

void TcpManager::Shutdown()
{
    shuttingDown = true;
    auto clientsCopy = clients;
    for (auto& client : clientsCopy)
        client->RequestClose();
    auto serversCopy = servers;
    for (auto& server : serversCopy)
        server->RequestClose();
}

} // namespace lodetcp
