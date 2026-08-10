// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tty/TtyManager.hpp"
#include "Tty/TtyStream.hpp"
#include <algorithm>

namespace lodetty
{

void TtyManager::AddStream(const std::shared_ptr<TtyStream>& stream)
{
    streams.push_back(stream);
}

void TtyManager::RemoveStream(const std::shared_ptr<TtyStream>& stream)
{
    auto it = std::find(streams.begin(), streams.end(), stream);
    if (it != streams.end())
        streams.erase(it);
}

void TtyManager::Shutdown()
{
    shuttingDown = true;
    auto streamsCopy = streams;
    for (auto& stream : streamsCopy)
        stream->RequestClose();
}

} // namespace lodetty