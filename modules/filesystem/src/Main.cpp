// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/FsHelpers.hpp"
#include "FileSystem/FsStatic.hpp"
#include "FileSystem/FsClasses.hpp"
#include "FileSystem/FileHandle.hpp"
#include "FileSystem/ReadStream.hpp"
#include "FileSystem/FileWatcher.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"

namespace lodefs {
    void FsManager::AddFile(const std::shared_ptr<FileHandle>& file) { files.push_back(file); }
    void FsManager::AddStream(const std::shared_ptr<ReadStream>& stream) { streams.push_back(stream); }
    void FsManager::AddWatcher(const std::shared_ptr<FileWatcher>& watcher) { watchers.push_back(watcher); }

    void FsManager::Shutdown()
    {
        if (shuttingDown) return;
        shuttingDown = true;

        for (auto& weak : watchers)
            if (auto watcher = weak.lock()) watcher->RequestClose();
        for (auto& weak : streams)
            if (auto stream = weak.lock()) stream->RequestClose();
        for (auto& weak : files)
            if (auto file = weak.lock()) file->RequestClose();

        watchers.clear();
        streams.clear();
        files.clear();
    }
}

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodefs::FsManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();

    Lode::Exports exports(vm);

    lodefs::BindStaticMethods(vm, exports);
    lodefs::BindClasses(vm, exports, mgr);

    Lode::Task::RegisterShutdownHook(vm, [mgr]() {
        mgr->Shutdown();
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}
