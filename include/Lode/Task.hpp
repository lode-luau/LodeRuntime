#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "Lode/Coroutine.hpp"
#include <vector>
#include <functional>

namespace Lode
{

class LODE_API Task
{
public:
    // Yields the current calling coroutine for delayMs milliseconds
    static void Wait(State& vm, double delayMs);

    // Schedules a single callback execution after delayMs milliseconds (OS-level setTimeout)
    static int SetTimeout(State& vm, const Value& callback, double delayMs);
    static void ClearTimeout(State& vm, int timerId);

    // Schedules a recurring callback execution every intervalMs milliseconds (OS-level setInterval)
    static int SetInterval(State& vm, const Value& callback, double intervalMs);
    static void ClearInterval(State& vm, int timerId);

    // Immediately spawns a function or coroutine
    static Coroutine Spawn(State& vm, const Value& fnOrCo, const std::vector<Value>& args = {});

    // Defers a function/coroutine execution to the next event loop tick
    static void Defer(State& vm, const Value& fnOrCo, const std::vector<Value>& args = {});

    // Executes a function/coroutine after delayMs milliseconds
    static void Delay(State& vm, double delayMs, const Value& fnOrCo, const std::vector<Value>& args = {});

    // Cancels a scheduled task or coroutine
    static void Cancel(State& vm, const Value& target);
};

} // namespace Lode
