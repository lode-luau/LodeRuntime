// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

#include <functional>
#include <mutex>
#include <deque>
#include <vector>

struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;
struct uv_async_s;

namespace Lode
{

class State;

/**
 * @brief Memory/CPU budget for the GC hook driven from EventLoop::Run().
 *
 * All sizes are in kilobytes; leaving a field at 0 disables that knob:
 * stepSizeKB == 0 lets Luau pick its automatic step size, and zeroed soft/hard
 * limits turn the memory thresholds off entirely (the default).
 */
struct LODE_API GcBudget
{
    int stepSizeKB = 0;     ///< Incremental GC step size; 0 = Luau automatic step size.
    double softLimitKB = 0; ///< 0 = disabled; above this the hook runs an extra larger GC step.
    double hardLimitKB = 0; ///< 0 = disabled; above this the hook forces a full collection.
};

/**
 * @brief Represents a libuv-based event loop for asynchronous operations.
 */
class LODE_API EventLoop
{
public:
    /** @brief Constructs a new, isolated event loop. */
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /** @brief Runs the event loop continuously until no active events remain. */
    void Run(State& vm);
    /** @brief Runs a single iteration (step) of the event loop. */
    void Step(State& vm);
    /** @brief Stops the event loop. */
    void Stop();

    /**
     * @brief Configures the incremental GC driven from Run().
     *
     * The values are only stored here: a Run() already in progress keeps the
     * budget it captured at entry and the new settings apply from the next
     * Run() call onwards.
     */
    void SetGcBudget(const GcBudget& budget);

    /** @brief Closes the loop after all owned handles have been drained. */
    void Close();

    /**
     * @brief Registers work that must run before libuv handles are closed.
     *
     * Native modules use this to make foreign entrypoints inert before the
     * loop is torn down. Hooks run once, on the event-loop thread, at the
     * beginning of Close().
     */
    void AddCloseHook(std::function<void()> hook);

    /** Queues work from any native thread onto this loop's owner thread. */
    bool Post(std::function<void()> work);

    /** @brief Retrieves the underlying raw libuv loop pointer. */
    [[nodiscard]] uv_loop_t* GetUVLoop() const { return loop_; }

private:
    uv_loop_t* loop_ = nullptr;
    GcBudget gcBudget_;         ///< Disabled by default (all fields zero).
    bool gcHookActive_ = false; ///< True while a Run() owns the GC prepare hook.
    std::vector<std::function<void()>> closeHooks_;
    bool closing_ = false;
    ::uv_async_s* postAsync_ = nullptr;
    std::mutex postMutex_;
    std::deque<std::function<void()>> posted_;
};

} // namespace Lode
