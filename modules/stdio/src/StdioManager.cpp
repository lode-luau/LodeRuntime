// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Stdio/StdioManager.hpp"
#include "Stdio/StdioStream.hpp"
#include <algorithm>

namespace lodestdio
{

void StdioManager::AddStream(const std::shared_ptr<StdioStream>& stream)
{
    streams.push_back(stream);
}

void StdioManager::RemoveStream(const std::shared_ptr<StdioStream>& stream)
{
    auto it = std::find(streams.begin(), streams.end(), stream);
    if (it != streams.end())
    {
        streams.erase(it);
    }
}

void StdioManager::Shutdown()
{
    shuttingDown = true;
    auto copy = streams;
    for (auto& s : copy)
    {
        if (s)
            s->RequestClose();
    }
    streams.clear();
    stdinStream.reset();
    stdoutStream.reset();
    stderrStream.reset();
}

} // namespace lodestdio
