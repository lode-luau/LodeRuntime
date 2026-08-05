// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#define NOMINMAX
#include "uv.h"
#include "Lode/Logger.hpp"
#include <unordered_map>
#include <atomic>
#include <vector>

namespace Lode
{

static std::atomic<int> g_nextTimerId{ 1 };

static lua_State* g_mainThread = nullptr;
static std::string g_mainThreadError;

void Task::SetMainThread(lua_State* L)
{
    g_mainThread = L;
    g_mainThreadError.clear();
}

std::string Task::GetMainThreadError()
{
    std::string error = g_mainThreadError;
    g_mainThreadError.clear();
    return error;
}

struct TimerData
{
    int timerId = 0;
    lua_State* L = nullptr;
    Value callback;
    Coroutine coroutine;
    bool recurring = false;
    std::vector<Value> args;
    uv_timer_t handle{};
};

static std::unordered_map<int, TimerData*> g_activeTimers;

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

int Task::Wait(State& vm, double seconds)
{
    auto* timerData = new TimerData();
    timerData->timerId = g_nextTimerId++;
    timerData->L = vm.GetLuaState();
    timerData->coroutine = Coroutine(vm.GetLuaState());
    timerData->recurring = false;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->coroutine.IsValid())
        {
            auto res = data->coroutine.Resume();
            if (res.IsError())
            {
                if (g_mainThread && data->coroutine.GetThreadState() == g_mainThread)
                {
                    g_mainThreadError = res.GetError().ErrorMessage();
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
        uv_timer_stop(handle);
        SafeDestroyTimer(data);
    };

    uv_update_time(loop);
    uint64_t timeout = static_cast<uint64_t>(seconds > 0 ? seconds * 1000.0 : 1);
    uv_timer_start(&timerData->handle, onTimer, timeout, 0);

    return vm.YieldThread();
}

int Task::SetTimeout(State& vm, const Value& callback, double delayMs, const std::vector<Value>& args)
{
    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetLuaState();
    timerData->callback = callback;
    timerData->recurring = false;
    timerData->args = args;

    g_activeTimers[id] = timerData;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
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
            g_activeTimers.erase(data->timerId);
        }
        uv_timer_stop(handle);
        SafeDestroyTimer(data);
    };

    uv_update_time(loop);
    uint64_t timeout = static_cast<uint64_t>(delayMs > 0 ? delayMs : 1);
    uv_timer_start(&timerData->handle, onTimer, timeout, 0);

    return id;
}

void Task::ClearTimeout(State& vm, int timerId)
{
    auto it = g_activeTimers.find(timerId);
    if (it != g_activeTimers.end())
    {
        TimerData* data = it->second;
        uv_timer_stop(&data->handle);
        g_activeTimers.erase(it);
        SafeDestroyTimer(data);
    }
}

int Task::SetInterval(State& vm, const Value& callback, double intervalMs, const std::vector<Value>& args)
{
    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = vm.GetLuaState();
    timerData->callback = callback;
    timerData->recurring = true;
    timerData->args = args;

    g_activeTimers[id] = timerData;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
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
    };

    uv_update_time(loop);
    uint64_t repeat = static_cast<uint64_t>(intervalMs > 0 ? intervalMs : 1);
    uv_timer_start(&timerData->handle, onTimer, repeat, repeat);

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
