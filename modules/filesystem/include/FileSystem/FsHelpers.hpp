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

struct FsManager
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;
    
    Lode::Table fileMethods;
    Lode::Table readStreamMethods;
    Lode::Table watcherMethods;
    
    void Shutdown();
};

} // namespace lodefs
