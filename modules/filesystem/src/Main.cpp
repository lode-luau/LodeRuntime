// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/FsHelpers.hpp"
#include "FileSystem/FsStatic.hpp"
#include "FileSystem/FsClasses.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"

namespace lodefs {
    void FsManager::Shutdown()
    {
        shuttingDown = true;
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
