// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Sys/SysExport.hpp"

namespace lodesys
{

// =======================================================
// Native C++ APIs (Exported for other modules)
// =======================================================
SYS_API std::string GuessHandleType(int fd);

// =======================================================
// Luau Bindings
// =======================================================
SYS_API void BindSysProcess(Lode::State& vm, Lode::Table& exports);

} // namespace lodesys
