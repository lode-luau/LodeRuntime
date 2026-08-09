// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include "TcpManager.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include <memory>
#include <string>

namespace lodetcp
{

TCP_API int StartResolve(Lode::State& vm, const std::shared_ptr<TcpManager>& mgr, const std::string& host,
                         const Lode::Coroutine& co, const Lode::Value& callback);

} // namespace lodetcp
