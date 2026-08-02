#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "lua.h"
#include "uv.h"
#include <unordered_map>
#include <atomic>

namespace Lode
{

static std::atomic<int> g_nextTimerId{ 1 };

struct TimerData
{
    int timerId;
    lua_State* L;
    int callbackRef;
    bool recurring;
    uv_timer_t handle;
};

static std::unordered_map<int, TimerData*> g_activeTimers;

void Task::Wait(State& vm, double delayMs)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return;

    int coRef = lua_ref(L, 1);

    auto* timerData = new TimerData();
    timerData->timerId = g_nextTimerId++;
    timerData->L = L;
    timerData->callbackRef = coRef;
    timerData->recurring = false;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
        {
            lua_State* co = data->L;
            lua_resume(co, nullptr, 0);
            lua_unref(co, data->callbackRef);
        }
        uv_timer_stop(handle);
        delete data;
    };

    uint64_t timeout = static_cast<uint64_t>(delayMs > 0 ? delayMs : 1);
    uv_timer_start(&timerData->handle, onTimer, timeout, 0);

    vm.YieldThread();
}

int Task::SetTimeout(State& vm, const Value& callback, double delayMs)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return -1;

    callback.PushToLuaState(L);
    int cbRef = lua_ref(L, -1);

    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = L;
    timerData->callbackRef = cbRef;
    timerData->recurring = false;

    g_activeTimers[id] = timerData;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
        {
            lua_State* L = data->L;
            lua_getref(L, data->callbackRef);
            lua_pcall(L, 0, 0, 0);
            lua_unref(L, data->callbackRef);
            g_activeTimers.erase(data->timerId);
        }
        uv_timer_stop(handle);
        delete data;
    };

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
        if (data->L && data->callbackRef != LUA_NOREF)
        {
            lua_unref(data->L, data->callbackRef);
        }
        delete data;
        g_activeTimers.erase(it);
    }
}

int Task::SetInterval(State& vm, const Value& callback, double intervalMs)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return -1;

    callback.PushToLuaState(L);
    int cbRef = lua_ref(L, -1);

    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = L;
    timerData->callbackRef = cbRef;
    timerData->recurring = true;

    g_activeTimers[id] = timerData;

    uv_loop_t* loop = EventLoop::Default().GetUVLoop();
    uv_timer_init(loop, &timerData->handle);
    timerData->handle.data = timerData;

    auto onTimer = [](uv_timer_t* handle) {
        auto* data = static_cast<TimerData*>(handle->data);
        if (data && data->L)
        {
            lua_State* L = data->L;
            lua_getref(L, data->callbackRef);
            lua_pcall(L, 0, 0, 0);
        }
    };

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
    lua_State* L = vm.GetLuaState();
    if (!L) return Coroutine();

    if (fnOrCo.GetType() == ValueType::Function)
    {
        fnOrCo.PushToLuaState(L);
        int fnRef = lua_ref(L, -1);
        Coroutine co(L, fnRef);
        co.Resume(args);
        lua_unref(L, fnRef);
        return co;
    }
    return Coroutine();
}

void Task::Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, 0);
}

void Task::Delay(State& vm, double delayMs, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, delayMs);
}

void Task::Cancel(State& vm, const Value& target)
{
    if (target.GetType() == ValueType::Number || target.GetType() == ValueType::Integer)
    {
        ClearTimeout(vm, static_cast<int>(target.AsNumber()));
    }
}

} // namespace Lode
