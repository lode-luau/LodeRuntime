// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Sys/SysInfo.hpp"
#include "Sys/SysProcess.hpp"
#include "Sys/SysSignals.hpp"

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    
    auto procMgr = std::make_shared<lodesys::ProcessManager>();
    procMgr->mainL = vm.GetMainThread();
    procMgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [procMgr]() { procMgr->Shutdown(); });

    lodesys::BindSysInfo(vm, exports);
    lodesys::BindSysProcess(vm, exports, procMgr);
    lodesys::BindSysSignals(vm, exports);
    
    return { exports };
}
