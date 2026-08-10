// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "FileSystem/FsHelpers.hpp"
#include "Lode/Table.hpp"
#include "Lode/Signal.hpp"

namespace lodefs
{

class FileWatcher : public std::enable_shared_from_this<FileWatcher>
{
public:
    std::shared_ptr<FsManager> mgr;
    std::shared_ptr<FileWatcher> selfGuard;
    
    std::shared_ptr<Lode::Signal> changed;
    std::shared_ptr<Lode::Signal> errorOccurred;
    
    uv_fs_event_t watcher;
    std::string targetPath;
    bool watching = false;
    bool closing = false;
    bool closed = false;
    
    FileWatcher(std::shared_ptr<FsManager> m);
    ~FileWatcher();
    
    void RequestClose();
    void FinishClosed();
    void CheckClosed();
    
    static void OnEvent(uv_fs_event_t* handle, const char* filename, int events, int status);
    
    Lode::Value MethodStart(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodStop(Lode::State& vm, const std::vector<Lode::Value>& args);
};

Lode::Value WrapFileWatcher(Lode::State& vm, const std::shared_ptr<FileWatcher>& watcher, const Lode::Table& methods);

} // namespace lodefs
