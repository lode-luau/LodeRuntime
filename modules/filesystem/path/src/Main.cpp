// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"
#include "Path.hpp"

LODE_MODULE(vm)
{
    Lode::Exports exports(vm);
    lodefs::path::BindPathMethods(exports);
    return Lode::ModuleReturn(exports.GetExportTable());
}
