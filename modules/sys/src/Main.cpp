// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Sys/SysInfo.hpp"
#include "Sys/SysProcess.hpp"
#include "Sys/SysSignals.hpp"

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    
    lodesys::BindSysInfo(vm, exports);
    lodesys::BindSysProcess(vm, exports);
    lodesys::BindSysSignals(vm, exports);
    
    return { exports };
}
