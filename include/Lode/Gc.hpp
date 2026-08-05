// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"

namespace Lode
{

/**
 * @brief Per-State control over Luau's incremental garbage collector.
 *
 * Luau's GC is not global — it belongs to each lua_State. Lode scripts reach
 * it via the standard `collectgarbage()` builtin, but native code has no direct
 * handle. Gc wraps lua_gc so the runtime and native modules can drive the
 * collector for a specific State: force a collection cycle, run an incremental
 * step, pause/resume, tune the goal and step multiplier, and query memory and
 * status. All methods are no-op if the State has no lua_State.
 *
 * Typical use: after releasing a large native resource, call Gc::Collect(vm)
 * to reclaim the now-dead objects immediately instead of waiting for the
 * incremental sweep; or call Gc::Step(vm, N) inside a hot loop to amortise
 * collection work across frames.
 */
class LODE_API Gc
{
public:
    /**
     * @brief Run a full collection cycle (stop-the-world).
     *
     * Equivalent to `collectgarbage("collect")`. Effective at reclaiming memory
     * but can introduce a noticeable pause, so prefer Gc::Step for
     * latency-sensitive paths.
     */
    static void Collect(State& vm);

    /**
     * @brief Perform an explicit incremental GC step of `stepSizeKB` kilobytes.
     *
     * Triggers the start of the next collection cycle if the GC is currently
     * paused. Returns true if the step completed the current collection cycle.
     */
    static bool Step(State& vm, int stepSizeKB = 0);

    /** @brief Pause the incremental collector (equivalent to `collectgarbage("stop")`). */
    static void Stop(State& vm);

    /** @brief Resume the incremental collector (equivalent to `collectgarbage("restart")`). */
    static void Restart(State& vm);

    /**
     * @brief Set the GC goal G (the ratio of total heap to live data, in percent).
     *
     * The collector tries to keep the heap at roughly G% of the live-data size.
     * Default is 200 (heap may grow to ~2x live data). Lower values reclaim
     * memory faster but collect more often.
     */
    static void SetGoal(State&, int goal = 200);

    /**
     * @brief Set the GC step multiplier S (how aggressively it collects, in percent).
     *
     * The collector collects S% of allocated bytes each step. Higher values
     * keep the heap tighter but cost more CPU. Default is 200.
     */
    static void SetStepMultiplier(State&, int stepMul = 200);

    /**
     * @brief Set the explicit step size in KB (normally best left at the default 0).
     * When non-zero, Gc::Step uses this size instead of the automatic one.
     */
    static void SetStepSize(State&, int stepSizeKB = 0);

    /** @brief Return the current heap size in kilobytes (equivalent to `collectgarbage("count")`). */
    static double UsedMemoryKB(State& vm);

    /** @brief Return true if the incremental collector is active (not stopped). */
    static bool IsRunning(State& vm);

    /** @brief Return true if the collector is currently paused (between cycles). */
    static bool IsPaused(State& vm);
};

} // namespace Lode
