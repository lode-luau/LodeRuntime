// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Http/HttpManager.hpp"
#include "Http/HttpClient.hpp"
#include "Http/HttpServer.hpp"
#include <algorithm>

namespace lodehttp
{

void HttpManager::AddClient(const std::shared_ptr<HttpClient>& client) 
{ 
    clients.push_back(client); 
}

void HttpManager::AddServer(const std::shared_ptr<HttpServer>& server) 
{ 
    servers.push_back(server); 
}

void HttpManager::RemoveClient(const std::shared_ptr<HttpClient>& client)
{
    auto it = std::find(clients.begin(), clients.end(), client);
    if (it != clients.end())
        clients.erase(it);
}

void HttpManager::RemoveServer(const std::shared_ptr<HttpServer>& server)
{
    auto it = std::find(servers.begin(), servers.end(), server);
    if (it != servers.end())
        servers.erase(it);
}

void HttpManager::Shutdown()
{
    shuttingDown = true;
    auto clientsCopy = clients;
    for (auto& client : clientsCopy)
        client->RequestClose();
    auto serversCopy = servers;
    for (auto& server : serversCopy)
        server->RequestClose();
}

} // namespace lodehttp
