// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Pipe/PipeManager.hpp"
#include "Pipe/PipeStream.hpp"
#include "Pipe/PipeServer.hpp"
#include <algorithm>

namespace lodepipe
{

void PipeManager::AddStream(const std::shared_ptr<PipeStream>& stream)
{
    streams.push_back(stream);
}

void PipeManager::AddServer(const std::shared_ptr<PipeServer>& server)
{
    servers.push_back(server);
}

void PipeManager::RemoveStream(const std::shared_ptr<PipeStream>& stream)
{
    auto it = std::find(streams.begin(), streams.end(), stream);
    if (it != streams.end())
        streams.erase(it);
}

void PipeManager::RemoveServer(const std::shared_ptr<PipeServer>& server)
{
    auto it = std::find(servers.begin(), servers.end(), server);
    if (it != servers.end())
        servers.erase(it);
}

void PipeManager::Shutdown()
{
    shuttingDown = true;
    auto streamsCopy = streams;
    for (auto& stream : streamsCopy)
        stream->RequestClose();
    auto serversCopy = servers;
    for (auto& server : serversCopy)
        server->RequestClose();
}

} // namespace lodepipe