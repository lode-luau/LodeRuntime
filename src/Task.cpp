// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "TaskContext.hpp"
#define NOMINMAX
#include "uv.h"
#include "lua.h"
#include "Lode/Logger.hpp"
#include "Lode/Numeric.hpp"
#include <unordered_map>
#include <vector>
#include <exception>
#include <cmath>
#include <limits>

namespace Lode
{

static const char* const kTaskCtxKey = "_LODE_TASK_CTX";

static TaskContext* GetContext(lua_State* L)
{
    if (!L) return nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);
    auto* ctx = static_cast<TaskContext*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

static TaskContext* GetOrCreateContext(lua_State* L)
{
    if (!L) return nullptr;
    if (TaskContext* existing = GetContext(L))
        return existing;

    auto* mem = static_cast<TaskContext*>(lua_newuserdatadtor(L, sizeof(TaskContext), [](void* ptr) {
        static_cast<TaskContext*>(ptr)->~TaskContext();
    }));
    new (mem) TaskContext();
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);
    lua_pop(L, 1);
    return mem;
}

static Coroutine MakeTaskCoroutine(State& vm, const Value& target)
{
    return target.IsThread() ? target.AsCoroutine() : Coroutine(vm, target);
}

static void EmitTaskError(const std::string& message)
{
    Diagnostic diag;
    diag.message = message;
    diag.code = "TaskError";
    Logger::EmitDiagnostic(diag);
}

void Task::SetMainThread(State& vm, lua_State* L)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (ctx)
    {
        ctx->mainThread = L;
        ctx->mainThreadError.clear();
    }
}

std::string Task::GetMainThreadError(State& vm)
{
    TaskContext* ctx = GetContext(vm.GetMainThread());
    if (!ctx) return "";
    std::string error = ctx->mainThreadError;
    ctx->mainThreadError.clear();
    return error;
}

struct TimerData
{
    int timerId = 0;
    lua_State* L = nullptr;
    TaskContext* ctx = nullptr;
    Value callback;
    Coroutine coroutine;
    bool recurring = false;
    std::vector<Value> args;
    // task.wait(): resume the waiter with the real elapsed time in seconds.
    bool resumeWithElapsed = false;
    uint64_t startedAtNs = 0;
    uv_timer_t handle{};
};

static void SafeDestroyTimer(TimerData* data)
{
    if (!data) return;

    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&data->handle)))
    {
        uv_close(reinterpret_cast<uv_handle_t*>(&data->handle), [](uv_handle_t* handle) {
            auto* d = static_cast<TimerData*>(handle->data);
            handle->data = nullptr;
            delete d;
        });
    }
}

// Shared timer callback: resumes the wait coroutine or the scheduled
// callback and reports failures through the diagnostic channel.
static void OnTimerFired(uv_timer_t* handle)
{
    auto* data = static_cast<TimerData*>(handle->data);
    if (!data) return;

    if (data->coroutine.IsValid())
    {
        try
        {
            std::vector<Value> resumeArgs = data->args;
            if (data->resumeWithElapsed)
            {
                const double elapsedSeconds = static_cast<double>(uv_hrtime() - data->startedAtNs) / 1e9;
                resumeArgs.push_back(Value(elapsedSeconds));
            }
            auto res = data->coroutine.Resume(resumeArgs);
            if (res.IsError())
            {
                if (data->ctx && data->ctx->mainThread &&
                    data->coroutine.GetThreadState() == data->ctx->mainThread)
                {
                    data->ctx->mainThreadError = res.GetError().ErrorMessage();
                    uv_stop(handle->loop);
                }
                else
                {
                    EmitTaskError("Unhandled exception in task timer: " + res.GetError().ErrorMessage());
                }
            }
        }
        catch (const std::exception& e)
        {
            EmitTaskError(std::string("Unhandled C++ exception in task timer: ") + e.what());
        }
        catch (...)
        {
            EmitTaskError("Unhandled unknown C++ exception in task timer");
        }
    }
    else if (data->L)
    {
        const char* kind = data->recurring ? "SetInterval" : "SetTimeout";
        try
        {
            State localVm(data->L);
            Coroutine co = MakeTaskCoroutine(localVm, data->callback);
            auto res = co.Resume(data->args);
            if (res.IsError())
            {
                EmitTaskError(std::string("Unhandled exception in ") + kind + ": " + res.GetError().ErrorMessage());
            }
        }
        catch (const std::exception& e)
        {
            EmitTaskError(std::string("Unhandled C++ exception in ") + kind + ": " + e.what());
        }
        catch (...)
        {
            EmitTaskError(std::string("Unhandled unknown C++ exception in ") + kind);
        }
    }

    if (!data->recurring)
    {
        uv_timer_stop(handle);
        if (data->ctx)
            data->ctx->timers.erase(data->timerId);
        SafeDestroyTimer(data);
    }
}

// Shared timer setup: initializes and registers the uv handle, validates the
// duration, and starts the timer. Raises a Lua error and frees the timer on
// any failure. Returns true when the timer is running.
static bool StartTimer(State& vm, TaskContext* ctx, const char* label, TimerData* data,
                       double duration, double scale, const char* durationLabel, bool recurring)
{
    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    int initStatus = loop ? uv_timer_init(loop, &data->handle) : UV_EINVAL;
    if (initStatus != 0)
    {
        SafeDestroyTimer(data);
        vm.RaiseError(std::string("Lode::Task::") + label + ": failed to initialize timer handle (" +
                      uv_strerror(initStatus) + ")");
        return false;
    }

    ctx->timers[data->timerId] = data;
    data->handle.data = data;

    uv_update_time(loop);
    auto durationResult = Numeric::ToMilliseconds(duration, scale, durationLabel);
    if (durationResult.IsError())
    {
        ctx->timers.erase(data->timerId);
        SafeDestroyTimer(data);
        vm.RaiseError(durationResult.GetError().ErrorMessage());
        return false;
    }
    uint64_t timeout = durationResult.GetValue();
    if (timeout == 0) timeout = 1;
    int startStatus = uv_timer_start(&data->handle, OnTimerFired, timeout, recurring ? timeout : 0);
    if (startStatus != 0)
    {
        ctx->timers.erase(data->timerId);
        SafeDestroyTimer(data);
        vm.RaiseError(std::string("Failed to start ") + label + " timer: " + uv_strerror(startStatus));
        return false;
    }
    return true;
}

TaskContext::~TaskContext()
{
    // Normally empty: Task::Shutdown closes and flushes every timer before
    // destroying the context. This is only a defensive fallback.
    for (auto& [id, data] : timers)
    {
        (void)id;
        SafeDestroyTimer(data);
    }
    timers.clear();
}

void Task::Shutdown(State& vm)
{
    lua_State* L = vm.GetMainThread();
    TaskContext* ctx = GetContext(L);
    if (!ctx) return;

    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    for (auto& [id, data] : ctx->timers)
    {
        (void)id;
        uv_timer_stop(&data->handle);
        SafeDestroyTimer(data);
    }
    ctx->timers.clear();

    auto shutdownHooks = std::move(ctx->shutdownHooks);
    for (auto& hook : shutdownHooks)
    {
        if (hook)
            hook();
    }
    shutdownHooks.clear();

    // The userdata destructor owns the context lifetime. The timer and hook
    // containers are cleared here while the Lua state is still usable.
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);

    // Flush pending uv_close callbacks so the TimerData heap objects are freed now.
    if (loop)
        uv_run(loop, UV_RUN_NOWAIT);
}

void Task::RegisterShutdownHook(State& vm, std::function<void()> hook)
{
    if (!hook) return;
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (ctx)
        ctx->shutdownHooks.push_back(std::move(hook));
}

int Task::Wait(State& vm, double seconds)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (!ctx) return 0;

    auto* timerData = new TimerData();
    timerData->timerId = ctx->nextTimerId++;
    timerData->L = vm.GetLuaState();
    timerData->ctx = ctx;
    timerData->coroutine = Coroutine(vm.GetLuaState());
    timerData->recurring = false;
    timerData->resumeWithElapsed = true;
    timerData->startedAtNs = uv_hrtime();

    if (!StartTimer(vm, ctx, "wait", timerData, seconds, 1000.0, "wait duration", false))
        return 0;

    return vm.YieldThread();
}

int Task::SetTimeout(State& vm, const Value& callback, double delayMs, const std::vector<Value>& args)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (!ctx) return 0;

    int id = ctx->nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetMainThread();
    timerData->ctx = ctx;
    timerData->callback = callback;
    timerData->recurring = false;
    timerData->args = args;

    return StartTimer(vm, ctx, "timeout", timerData, delayMs, 1.0, "timeout delay", false) ? id : -1;
}

void Task::ClearTimeout(State& vm, int timerId)
{
    TaskContext* ctx = GetContext(vm.GetMainThread());
    if (!ctx) return;
    auto it = ctx->timers.find(timerId);
    if (it != ctx->timers.end())
    {
        TimerData* data = it->second;
        uv_timer_stop(&data->handle);
        ctx->timers.erase(it);
        SafeDestroyTimer(data);
    }
}

int Task::SetInterval(State& vm, const Value& callback, double intervalMs, const std::vector<Value>& args)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (!ctx) return 0;

    int id = ctx->nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetMainThread();
    timerData->ctx = ctx;
    timerData->callback = callback;
    timerData->recurring = true;
    timerData->args = args;

    if (!StartTimer(vm, ctx, "interval", timerData, intervalMs, 1.0, "interval duration", true))
        return -1;

    return id;
}

void Task::ClearInterval(State& vm, int timerId)
{
    ClearTimeout(vm, timerId);
}

Coroutine Task::Spawn(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    Coroutine co = MakeTaskCoroutine(vm, fnOrCo);
    auto res = co.Resume(args);
    if (res.IsError())
    {
        Diagnostic diag;
        diag.message = "Unhandled exception in Spawn task: " + res.GetError().ErrorMessage();
        diag.code = "TaskError";
        Logger::EmitDiagnostic(diag);
    }
    return co;
}

Coroutine Task::Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (!ctx) return Coroutine();

    int id = ctx->nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetMainThread();
    timerData->ctx = ctx;
    timerData->coroutine = MakeTaskCoroutine(vm, fnOrCo);
    timerData->recurring = false;
    timerData->args = args;

    if (!StartTimer(vm, ctx, "defer", timerData, 0, 1.0, "defer delay", false))
    {
        return Coroutine();
    }
    return timerData->coroutine;
}

Coroutine Task::Delay(State& vm, double seconds, const Value& fnOrCo, const std::vector<Value>& args)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (!ctx) return Coroutine();

    int id = ctx->nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetMainThread();
    timerData->ctx = ctx;
    timerData->coroutine = MakeTaskCoroutine(vm, fnOrCo);
    timerData->recurring = false;
    timerData->args = args;

    if (!StartTimer(vm, ctx, "delay", timerData, seconds, 1000.0, "delay duration", false))
    {
        return Coroutine();
    }
    return timerData->coroutine;
}

void Task::Cancel(State& vm, const Value& target)
{
    if (target.IsThread())
    {
        Coroutine coroutine = target.AsCoroutine();
        TaskContext* ctx = GetContext(vm.GetMainThread());
        if (!ctx || !coroutine.IsValid()) return;
        std::vector<int> timerIds;
        for (const auto& [id, data] : ctx->timers)
        {
            if (data && data->coroutine.IsValid() &&
                data->coroutine.GetThreadState() == coroutine.GetThreadState())
                timerIds.push_back(id);
        }
        for (int id : timerIds) ClearTimeout(vm, id);
        return;
    }
    if (target.IsNumber() || target.IsInteger())
    {
        double value = target.AsNumber();
        if (std::isfinite(value) && value == std::trunc(value) &&
            value >= static_cast<double>(std::numeric_limits<int>::min()) &&
            value < static_cast<double>(std::numeric_limits<int>::max()) + 1.0)
            ClearTimeout(vm, static_cast<int>(value));
    }
}

bool Task::IsMainThread(State& vm, lua_State* L)
{
    TaskContext* ctx = GetContext(vm.GetMainThread());
    return ctx && ctx->mainThread == L;
}

void Task::SetMainThreadError(State& vm, std::string message)
{
    TaskContext* ctx = GetOrCreateContext(vm.GetMainThread());
    if (ctx)
    {
        ctx->mainThreadError = std::move(message);
        uv_stop(vm.GetEventLoop().GetUVLoop());
    }
}

} // namespace Lode
