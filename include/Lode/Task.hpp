// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "Lode/Coroutine.hpp"
#include <vector>
#include <functional>
#include <string>

namespace Lode
{

/**
 * @brief Provides a robust task scheduling API for the Lode event loop.
 * 
 * Supports setTimeout, setInterval, coroutine spawning, deferring, and yielding.
 * These methods heavily rely on the underlying libuv event loop.
 */
class LODE_API Task
{
public:
    /** 
     * @brief Yields the current calling coroutine for a specific duration in seconds. 
     * @return The number of values yielded to the Lua VM (internally used by C API).
     */
    static int Wait(State& vm, double seconds);

    /** @brief Schedules a single callback execution after delayMs milliseconds (OS-level setTimeout). */
    static int SetTimeout(State& vm, const Value& callback, double delayMs, const std::vector<Value>& args = {});
    /** @brief Clears a scheduled timeout. */
    static void ClearTimeout(State& vm, int timerId);

    /** @brief Schedules a recurring callback execution every intervalMs milliseconds (OS-level setInterval). */
    static int SetInterval(State& vm, const Value& callback, double intervalMs, const std::vector<Value>& args = {});
    /** @brief Clears a scheduled interval. */
    static void ClearInterval(State& vm, int timerId);

    /** @brief Immediately spawns a function or coroutine and executes it on a new logical thread. */
    static Coroutine Spawn(State& vm, const Value& fnOrCo, const std::vector<Value>& args = {});

    /** @brief Defers a function or coroutine execution to the next event loop tick. */
    static void Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args = {});

    /** @brief Executes a function or coroutine after a specific delay in seconds. */
    static void Delay(State& vm, double seconds, const Value& fnOrCo, const std::vector<Value>& args = {});

    /** @brief Cancels a scheduled task or coroutine. */
    static void Cancel(State& vm, const Value& target);

    /**
     * @brief Registers the top-level script thread.
     *
     * When that thread errors while being resumed from a Wait timer (the script
     * yielded, so it can only continue inside the event loop), the error is
     * captured instead of being swallowed as a TaskError so the runtime can
     * surface it as the script's fatal error.
     */
    static void SetMainThread(lua_State* L);

    /** @brief Returns the pending main-script error (cleared after retrieval). */
    static std::string GetMainThreadError();
};

} // namespace Lode
