// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once
#include "Lode/Module.hpp"
#include "FileSystem/FsHelpers.hpp"

namespace lodefs
{
    void BindClasses(Lode::State& vm, Lode::Exports& exports, std::shared_ptr<FsManager> mgr);
}
