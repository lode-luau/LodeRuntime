// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/EventLoop.hpp"
#include "Lode/Gc.hpp"
#include "Lode/Logger.hpp"
#include "Lode/State.hpp"
#include "uv.h"
#include <stdexcept>

namespace Lode
{

namespace
{
void CloseHandle(uv_handle_t* handle, void*)
{
    if (!uv_is_closing(handle))
        uv_close(handle, nullptr);
}

/**
 * @brief Stack-allocated context shared between EventLoop::Run and the GC
 * prepare callback.
 *
 * The callback reaches it through uv_handle_t::data, so it must outlive every
 * uv_run() performed while the hook is installed; no heap allocation is
 * involved.
 */
struct GcHookContext
{
    State& vm;
    GcBudget budget;
    bool hardLimitWarned = false;
};

/**
 * @brief Prepare callback: donates time to the incremental GC right before the
 * loop would block for I/O.
 */
void OnGcPrepare(uv_prepare_t* handle)
{
    auto* ctx = static_cast<GcHookContext*>(handle->data);
    if (!ctx)
        return;

    // Regular incremental step; stepSizeKB == 0 means Luau's automatic size.
    Gc::Step(ctx->vm, ctx->budget.stepSizeKB);

    // Soft limit: the heap grew past the comfort zone, donate one extra larger
    // step to pull it back down.
    if (ctx->budget.softLimitKB > 0 && Gc::UsedMemoryKB(ctx->vm) > ctx->budget.softLimitKB)
        Gc::Step(ctx->vm, 256);

    // Hard limit: reclaim everything at once instead of waiting for the
    // incremental sweep to catch up, warning once per crossing.
    if (ctx->budget.hardLimitKB > 0)
    {
        const double usedKB = Gc::UsedMemoryKB(ctx->vm);
        if (usedKB > ctx->budget.hardLimitKB)
        {
            Gc::Collect(ctx->vm);
            if (!ctx->hardLimitWarned)
            {
                Logger::Warn("GC hard memory limit exceeded; forcing full collection");
                ctx->hardLimitWarned = true;
            }
        }
        else
        {
            // Rearm the warning once the heap settles back down: below the
            // soft limit when one is configured, otherwise below the hard
            // limit itself.
            const double rearmKB = ctx->budget.softLimitKB > 0 ? ctx->budget.softLimitKB : ctx->budget.hardLimitKB;
            if (usedKB <= rearmKB)
                ctx->hardLimitWarned = false;
        }
    }
}
} // namespace

EventLoop::EventLoop()
{
    loop_ = new uv_loop_t;
    int status = uv_loop_init(loop_);
    if (status != 0)
    {
        delete loop_;
        loop_ = nullptr;
        throw std::runtime_error(std::string("Failed to initialize event loop: ") + uv_strerror(status));
    }
}

EventLoop::~EventLoop()
{
    Close();
}

void EventLoop::Run(State& vm)
{
    if (!loop_)
        return;

    // Install the GC prepare hook when any budget knob is active. It is
    // created and closed per Run() call, which keeps the REPL's repeated
    // Run() invocations simple; gcHookActive_ guards reentrant Run() calls
    // from installing a second hook.
    GcHookContext ctx{vm, gcBudget_};
    uv_prepare_t prepare{};
    bool hookInstalledHere = false;

    const bool budgetActive = gcBudget_.stepSizeKB > 0 || gcBudget_.softLimitKB > 0 || gcBudget_.hardLimitKB > 0;
    if (budgetActive && !gcHookActive_)
    {
        if (uv_prepare_init(loop_, &prepare) == 0)
        {
            prepare.data = &ctx;
            uv_prepare_start(&prepare, OnGcPrepare);
            // Unref immediately: a referenced prepare handle keeps the loop
            // alive forever and UV_RUN_DEFAULT would never return.
            uv_unref(reinterpret_cast<uv_handle_t*>(&prepare));
            gcHookActive_ = true;
            hookInstalledHere = true;
        }
    }

    uv_run(loop_, UV_RUN_DEFAULT);

    if (hookInstalledHere)
    {
        // Fully release the hook before the stack context dies, draining the
        // pending close callback the same way Close() does.
        uv_prepare_stop(&prepare);
        uv_close(reinterpret_cast<uv_handle_t*>(&prepare), nullptr);
        while (uv_loop_alive(loop_))
        {
            uv_run(loop_, UV_RUN_NOWAIT);
        }
        gcHookActive_ = false;
    }
}

void EventLoop::Step(State& vm)
{
    if (loop_)
    {
        uv_run(loop_, UV_RUN_NOWAIT);
    }
}

void EventLoop::Stop()
{
    if (loop_)
    {
        uv_stop(loop_);
    }
}

void EventLoop::SetGcBudget(const GcBudget& budget)
{
    // Stored only: a Run() already in progress captured the previous budget in
    // its hook context and finishes with it; the new values apply from the
    // next Run() call onwards.
    gcBudget_ = budget;
}

void EventLoop::AddCloseHook(std::function<void()> hook)
{
    if (!hook)
        return;
    if (closing_)
    {
        hook();
        return;
    }
    closeHooks_.push_back(std::move(hook));
}

void EventLoop::Close()
{
    if (!loop_)
        return;

    closing_ = true;
    auto hooks = std::move(closeHooks_);
    closeHooks_.clear();
    for (auto& hook : hooks)
    {
        try
        {
            hook();
        }
        catch (const std::exception& error)
        {
            Logger::Error(std::string("Event-loop close hook failed: ") + error.what());
        }
        catch (...)
        {
            Logger::Error("Event-loop close hook failed with an unknown exception");
        }
    }

    uv_stop(loop_);
    uv_walk(loop_, CloseHandle, nullptr);
    while (uv_loop_alive(loop_))
    {
        uv_run(loop_, UV_RUN_NOWAIT);
    }

    uv_loop_close(loop_);
    delete loop_;
    loop_ = nullptr;
}

} // namespace Lode
