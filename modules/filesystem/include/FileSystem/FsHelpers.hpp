// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/State.hpp"
#include <uv.h>
#include <string>
#include <memory>
#include <vector>

namespace lodefs
{

class FileHandle;
class ReadStream;
class FileWatcher;

struct FsManager
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;
    
    Lode::Table fileMethods;
    Lode::Table readStreamMethods;
    Lode::Table watcherMethods;

    std::vector<std::weak_ptr<FileHandle>> files;
    std::vector<std::weak_ptr<ReadStream>> streams;
    std::vector<std::weak_ptr<FileWatcher>> watchers;

    void AddFile(const std::shared_ptr<FileHandle>& file);
    void AddStream(const std::shared_ptr<ReadStream>& stream);
    void AddWatcher(const std::shared_ptr<FileWatcher>& watcher);
    
    void Shutdown();
};

} // namespace lodefs
