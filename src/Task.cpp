#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "lua.h"
#include "lualib.h"
#include "uv.h"
#include <unordered_map>
#include <atomic>
#include <vector>

namespace Lode
{

static std::atomic<int> g_nextTimerId{ 1 };

struct TimerData
{
    int timerId = 0;
    lua_State* L = nullptr;
    int callbackRef = LUA_NOREF;
    bool recurring = false;
    std::vector<Value> args;
    uv_timer_t handle{};
};

static std::unordered_map<int, TimerData*> g_activeTimers;

// Callback de encerramento seguro de memória da Libuv
static void SafeDestroyTimer(TimerData* data)
{
    if (!data) return;

    if (data->L && data->callbackRef != LUA_NOREF)
    {
        lua_unref(data->L, data->callbackRef);
        data->callbackRef = LUA_NOREF;
    }

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

void Task::Wait(State& vm, double delayMs)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return;

    // Garante referência da thread no Registry para evitar coleta pelo Garbage Collector
    lua_pushthread(L);
    int coRef = lua_ref(L, LUA_REGISTRYINDEX);

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
            
            // Retoma a corrotina suspensa
            int status = lua_resume(co, nullptr, 0);
            if (status != LUA_OK && status != LUA_YIELD)
            {
                // Trata erros de execução na retomada da corrotina se necessário
            }
        }
        uv_timer_stop(handle);
        SafeDestroyTimer(data);
    };

    uint64_t timeout = static_cast<uint64_t>(delayMs > 0 ? delayMs : 1);
    uv_timer_start(&timerData->handle, onTimer, timeout, 0);

    vm.YieldThread();
}

int Task::SetTimeout(State& vm, const Value& callback, double delayMs, const std::vector<Value>& args)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return -1;

    callback.PushToLuaState(L);
    int cbRef = lua_ref(L, LUA_REGISTRYINDEX);

    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = L;
    timerData->callbackRef = cbRef;
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
            lua_State* L = data->L;
            lua_getref(L, data->callbackRef);

            // Empurra os argumentos guardados para a chamada da função
            for (const auto& arg : data->args)
            {
                arg.PushToLuaState(L);
            }

            lua_pcall(L, static_cast<int>(data->args.size()), 0, 0);
            g_activeTimers.erase(data->timerId);
        }
        uv_timer_stop(handle);
        SafeDestroyTimer(data);
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
        g_activeTimers.erase(it);
        SafeDestroyTimer(data);
    }
}

int Task::SetInterval(State& vm, const Value& callback, double intervalMs, const std::vector<Value>& args)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return -1;

    callback.PushToLuaState(L);
    int cbRef = lua_ref(L, LUA_REGISTRYINDEX);

    int id = g_nextTimerId++;
    auto* timerData = new TimerData();
    timerData->timerId = id;
    timerData->L = L;
    timerData->callbackRef = cbRef;
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
            lua_State* L = data->L;
            lua_getref(L, data->callbackRef);

            for (const auto& arg : data->args)
            {
                arg.PushToLuaState(L);
            }

            lua_pcall(L, static_cast<int>(data->args.size()), 0, 0);
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

void Task::Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, 0, args);
}

void Task::Delay(State& vm, double delayMs, const Value& fnOrCo, const std::vector<Value>& args)
{
    SetTimeout(vm, fnOrCo, delayMs, args);
}

void Task::Cancel(State& vm, const Value& target)
{
    if (target.GetType() == ValueType::Number || target.GetType() == ValueType::Integer)
    {
        ClearTimeout(vm, static_cast<int>(target.AsNumber()));
    }
}

} // namespace Lode
