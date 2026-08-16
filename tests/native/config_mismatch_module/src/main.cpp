// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"

LODE_EXPORT int LodeModuleInit(lua_State* L)
{
    (void)L;
    return 0;
}

// Always report a configuration that can never match the running runtime, so
// this fixture deterministically exercises the loader's ABI-mismatch guard.
LODE_EXPORT const char* LodeModuleConfig()
{
    return "mismatch-fixture-config";
}