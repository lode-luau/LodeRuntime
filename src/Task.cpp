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

    auto* mem = static_cast<TaskContext*>(lua_newuserdata(L, sizeof(TaskContext)));
    new (mem) TaskContext();
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);
    lua_pop(L, 1);
    return mem;
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
    uv_timer_t handle{};
};

static void SafeDestroyTimer(TimerData* data)
{
    if (!data) return;

    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&data->handle)))
    {
        uv_close(reinterpret_cast<uv_handle_t*>(&data->handle), [](uv_handle_t* handle) {
            auto* d = static_cast<TimerData*>(handle->data);
            delete d;
        });
    }
    else
    {
        delete data;
    }
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
    if (!L) return;

    lua_getfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);
    TaskContext* ctx = static_cast<TaskContext*>(lua_touserdata(L, -1));
    if (!ctx)
    {
        lua_pop(L, 1);
        return;
    }

    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    for (auto& [id, data] : ctx->timers)
    {
        (void)id;
        uv_timer_stop(&data->handle);
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&data->handle)))
        {
            uv_close(reinterpret_cast<uv_handle_t*>(&data->handle), [](uv_handle_t* handle) {
                delete static_cast<TimerData*>(handle->data);
            });
        }
        else
        {
            delete data;
        }
    }
    ctx->timers.clear();

    auto shutdownHooks = std::move(ctx->shutdownHooks);
    for (auto& hook : shutdownHooks)
    {
        if (hook)
            hook();
    }
    shutdownHooks.clear();

    // Destroy the per-State context before the VM is released; the timers (and
    // the Value/Coroutine references they hold) are freed below while the VM is
    // still alive, so lua_unref runs against an open lua_State.
    ctx->~TaskContext();
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kTaskCtxKey);
    lua_pop(L, 1);

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
    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    int initStatus = loop ? uv_timer_init(loop, &timerData->handle) : UV_EINVAL;
    if (initStatus != 0)
    {
        delete timerData;
        vm.RaiseError(std::string("Failed to initialize wait timer: ") + uv_strerror(initStatus));
        return 0;
    }

    ctx->timers[timerData->timerId] = timerData;
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->coroutine.IsValid())
        {
            try
            {
                auto res = data->coroutine.Resume();
                if (res.IsError())
                {
                    if (data->ctx && data->ctx->mainThread &&
                        data->coroutine.GetThreadState() == data->ctx->mainThread)
                    {
                        data->ctx->mainThreadError = res.GetError().ErrorMessage();
                    }
                    else
                    {
                        Diagnostic diag;
                        diag.message = "Unhandled exception in Wait timer: " + res.GetError().ErrorMessage();
                        diag.code = "TaskError";
                        Logger::EmitDiagnostic(diag);
                    }
                }
            }
            catch (const std::exception& e)
            {
                Diagnostic diag;
                diag.message = std::string("Unhandled C++ exception in Wait timer: ") + e.what();
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
            catch (...)
            {
                Diagnostic diag;
                diag.message = "Unhandled unknown C++ exception in Wait timer";
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
        }
        uv_timer_stop(handle);
        if (data && data->ctx)
            data->ctx->timers.erase(data->timerId);
        SafeDestroyTimer(data);
    };

    uv_update_time(loop);
    auto timeoutResult = Numeric::ToMilliseconds(seconds, 1000.0, "wait duration");
    if (timeoutResult.IsError())
    {
        int timerId = timerData->timerId;
        ctx->timers.erase(timerId);
        SafeDestroyTimer(timerData);
        vm.RaiseError(timeoutResult.GetError().ErrorMessage());
        return 0;
    }
    uint64_t timeout = timeoutResult.GetValue();
    if (timeout == 0) timeout = 1;
    int startStatus = uv_timer_start(&timerData->handle, onTimer, timeout, 0);
    if (startStatus != 0)
    {
        ctx->timers.erase(timerData->timerId);
        SafeDestroyTimer(timerData);
        vm.RaiseError(std::string("Failed to start wait timer: ") + uv_strerror(startStatus));
        return 0;
    }

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

    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    int initStatus = loop ? uv_timer_init(loop, &timerData->handle) : UV_EINVAL;
    if (initStatus != 0)
    {
        delete timerData;
        return -1;
    }

    ctx->timers[id] = timerData;
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
        {
            try
            {
                State localVm(data->L);
                Coroutine co(localVm, data->callback);
                auto res = co.Resume(data->args);
                if (res.IsError())
                {
                    Diagnostic diag;
                    diag.message = "Unhandled exception in SetTimeout: " + res.GetError().ErrorMessage();
                    diag.code = "TaskError";
                    Logger::EmitDiagnostic(diag);
                }
            }
            catch (const std::exception& e)
            {
                Diagnostic diag;
                diag.message = std::string("Unhandled C++ exception in SetTimeout: ") + e.what();
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
            catch (...)
            {
                Diagnostic diag;
                diag.message = "Unhandled unknown C++ exception in SetTimeout";
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
        }
        uv_timer_stop(handle);
        if (data && data->ctx)
            data->ctx->timers.erase(data->timerId);
        SafeDestroyTimer(data);
    };

    uv_update_time(loop);
    auto timeoutResult = Numeric::ToMilliseconds(delayMs, 1.0, "timeout delay");
    if (timeoutResult.IsError())
    {
        ctx->timers.erase(id);
        SafeDestroyTimer(timerData);
        vm.RaiseError(timeoutResult.GetError().ErrorMessage());
        return -1;
    }
    uint64_t timeout = timeoutResult.GetValue();
    if (timeout == 0) timeout = 1;
    int startStatus = uv_timer_start(&timerData->handle, onTimer, timeout, 0);
    if (startStatus != 0)
    {
        ctx->timers.erase(id);
        SafeDestroyTimer(timerData);
        return -1;
    }

    return id;
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

    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    int initStatus = loop ? uv_timer_init(loop, &timerData->handle) : UV_EINVAL;
    if (initStatus != 0)
    {
        delete timerData;
        return -1;
    }

    ctx->timers[id] = timerData;
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
        {
            try
            {
                State localVm(data->L);
                Coroutine co(localVm, data->callback);
                auto res = co.Resume(data->args);
                if (res.IsError())
                {
                    Diagnostic diag;
                    diag.message = "Unhandled exception in SetInterval: " + res.GetError().ErrorMessage();
                    diag.code = "TaskError";
                    Logger::EmitDiagnostic(diag);
                }
            }
            catch (const std::exception& e)
            {
                Diagnostic diag;
                diag.message = std::string("Unhandled C++ exception in SetInterval: ") + e.what();
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
            catch (...)
            {
                Diagnostic diag;
                diag.message = "Unhandled unknown C++ exception in SetInterval";
                diag.code = "TaskError";
                Logger::EmitDiagnostic(diag);
            }
        }
    };

    uv_update_time(loop);
    auto repeatResult = Numeric::ToMilliseconds(intervalMs, 1.0, "interval duration");
    if (repeatResult.IsError())
    {
        ctx->timers.erase(id);
        SafeDestroyTimer(timerData);
        vm.RaiseError(repeatResult.GetError().ErrorMessage());
        return -1;
    }
    uint64_t repeat = repeatResult.GetValue();
    if (repeat == 0) repeat = 1;
    int startStatus = uv_timer_start(&timerData->handle, onTimer, repeat, repeat);
    if (startStatus != 0)
    {
        ctx->timers.erase(id);
        SafeDestroyTimer(timerData);
        return -1;
    }

    return id;
}

void Task::ClearInterval(State& vm, int timerId)
{
    ClearTimeout(vm, timerId);
}

Coroutine Task::Spawn(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    Coroutine co(vm, fnOrCo);
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

void Task::Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, 0, args);
}

void Task::Delay(State& vm, double seconds, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, seconds * 1000.0, args);
}

void Task::Cancel(State& vm, const Value& target)
{
    if (target.IsNumber() || target.IsInteger())
    {
        ClearTimeout(vm, static_cast<int>(target.AsNumber()));
    }
}

} // namespace Lode
