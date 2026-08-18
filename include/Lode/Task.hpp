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
    static Coroutine Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args = {});

    /** @brief Executes a function or coroutine after a specific delay in seconds. */
    static Coroutine Delay(State& vm, double seconds, const Value& fnOrCo, const std::vector<Value>& args = {});

    /** @brief Cancels a scheduled task or coroutine. */
    static void Cancel(State& vm, const Value& target);

    /**
     * @brief Registers the top-level script thread for a specific State.
     *
     * When that thread errors while being resumed from a Wait timer (the script
     * yielded, so it can only continue inside the event loop), the error is
     * captured instead of being swallowed as a TaskError so the runtime can
     * surface it as the script's fatal error. The registration is stored per
     * State, so independent States never clobber each other's fatal error slot.
     */
    static void SetMainThread(State& vm, lua_State* L);
    static bool IsMainThread(State& vm, lua_State* L);

    /** @brief Returns the pending main-script error (cleared after retrieval). */
    static std::string GetMainThreadError(State& vm);
    static void SetMainThreadError(State& vm, std::string message);

    /**
     * @brief Cancels every pending timer owned by the given State.
     *
     * Must be called before the State's lua_State is closed (State::~State does
     * this automatically) so no timer is left holding a dangling lua_State*.
     */
    static void Shutdown(State& vm);

    /** @brief Registers a State-owned asynchronous resource cleanup callback. */
    static void RegisterShutdownHook(State& vm, std::function<void()> hook);
};

} // namespace Lode
