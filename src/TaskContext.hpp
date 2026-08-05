// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

struct lua_State;

namespace Lode
{

// Owned per-State, stored in the Lua registry under "_LODE_TASK_CTX". Keeping
// it in the registry (like the module-loader navigation context) makes the timer
// table, the id counter, the main-thread registration and the fatal-error slot
// naturally per-lua_State instead of file-scope globals, so two live States no
// longer clobber each other. TimerData is defined in Task.cpp (it owns the
// libuv handle), and TaskContext::~TaskContext is defined there as well.
struct TimerData;

struct TaskContext
{
    int nextTimerId = 1;
    lua_State* mainThread = nullptr;
    std::string mainThreadError;
    std::unordered_map<int, TimerData*> timers;
    std::vector<std::function<void()>> shutdownHooks;

    TaskContext() = default;
    ~TaskContext();
    TaskContext(const TaskContext&) = delete;
    TaskContext& operator=(const TaskContext&) = delete;
};

} // namespace Lode
