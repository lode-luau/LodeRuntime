// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
// Stub: replaced by the full implementation in a later commit.

#include "Lode/Module.hpp"

LODE_MODULE(vm)
{
    Lode::Exports exports(vm);
    return Lode::ModuleReturn(exports.GetExportTable());
}
