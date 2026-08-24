// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lodesys
{

struct SignalWatcher;

struct SignalManager : std::enable_shared_from_this<SignalManager>
{
    Lode::State* vm = nullptr;
    bool shuttingDown = false;
    std::unordered_map<std::string, std::shared_ptr<SignalWatcher>> watchers;

    Lode::Value Watch(Lode::State& state, Lode::StackArgs args);
    void Shutdown();
};

void BindSysSignals(Lode::State& vm, Lode::Table& exports);

} // namespace lodesys
