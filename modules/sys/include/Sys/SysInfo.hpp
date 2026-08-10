// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Sys/SysExport.hpp"

namespace lodesys
{

SYS_API void BindSysInfo(Lode::State& vm, Lode::Table& exports);

} // namespace lodesys
