// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "Lode/Task.hpp"
#include <vector>

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    // task.wait(seconds) -> yields the calling coroutine for seconds.
    // The elapsed time is supplied by the timer resume path in Lode::Task
    // (src/Task.cpp resumes waiters with the real elapsed seconds).
    exports.Set("wait", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        double seconds = (args.Size() > 0 && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        Lode::Task::Wait(vm, seconds);
        return Lode::Value();
    }));

    // task.spawn(fnOrCo, ...) -> spawns a function or coroutine immediately
    exports.Set("spawn", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty())
        {
            return Lode::Value();
        }
        Lode::Value fnOrCo = args[0].ToValue();
        std::vector<Lode::Value> passArgs; for (auto it = args.begin() + 1; it != args.end(); ++it) passArgs.push_back(it->ToValue());
        Lode::Coroutine co = Lode::Task::Spawn(vm, fnOrCo, passArgs);
        return Lode::Value(co);
    }));

    // task.defer(fnOrCo, ...) -> defers execution to the next event loop tick
    exports.Set("defer", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty())
        {
            return Lode::Value();
        }
        Lode::Value fnOrCo = args[0].ToValue();
        std::vector<Lode::Value> passArgs; for (auto it = args.begin() + 1; it != args.end(); ++it) passArgs.push_back(it->ToValue());
        Lode::Coroutine co = Lode::Task::Defer(vm, fnOrCo, passArgs);
        return Lode::Value(co);
    }));

    // task.delay(delaySeconds, fnOrCo, ...) -> executes after delaySeconds
    exports.Set("delay", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2)
        {
            return Lode::Value();
        }
        double delaySeconds = args[0].IsNumber() ? args[0].AsNumber() : 0.0;
        Lode::Value fnOrCo = args[1].ToValue();
        std::vector<Lode::Value> passArgs; for (auto it = args.begin() + 2; it != args.end(); ++it) passArgs.push_back(it->ToValue());
        Lode::Coroutine co = Lode::Task::Delay(vm, delaySeconds, fnOrCo, passArgs);
        return Lode::Value(co);
    }));

    // task.cancel(target) -> cancels a scheduled task timer or coroutine
    exports.Set("cancel", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!args.empty())
        {
            Lode::Task::Cancel(vm, args[0].ToValue());
        }
        return Lode::Value();
    }));

    // task.setTimeout(callback, delayMs, ...) -> OS-level setTimeout returning timerId
    exports.Set("setTimeout", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2)
        {
            return Lode::Value(0);
        }
        Lode::Value callback = args[0].ToValue();
        double delayMs = args[1].IsNumber() ? args[1].AsNumber() : 0.0;
        std::vector<Lode::Value> passArgs;
        if (args.Size() > 2)
        {
            for (auto it = args.begin() + 2; it != args.end(); ++it) passArgs.push_back(it->ToValue());
        }
        int timerId = Lode::Task::SetTimeout(vm, callback, delayMs, passArgs);
        return Lode::Value(static_cast<double>(timerId));
    }));

    // task.clearTimeout(timerId) -> clears a timeout timer
    exports.Set("clearTimeout", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!args.empty() && args[0].IsNumber())
        {
            Lode::Task::ClearTimeout(vm, static_cast<int>(args[0].AsNumber()));
        }
        return Lode::Value();
    }));

    // task.setInterval(callback, intervalMs, ...) -> OS-level setInterval returning timerId
    exports.Set("setInterval", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2)
        {
            return Lode::Value(0);
        }
        Lode::Value callback = args[0].ToValue();
        double intervalMs = args[1].IsNumber() ? args[1].AsNumber() : 0.0;
        std::vector<Lode::Value> passArgs;
        if (args.Size() > 2)
        {
            for (auto it = args.begin() + 2; it != args.end(); ++it) passArgs.push_back(it->ToValue());
        }
        int timerId = Lode::Task::SetInterval(vm, callback, intervalMs, passArgs);
        return Lode::Value(static_cast<double>(timerId));
    }));

    // task.clearInterval(timerId) -> clears an interval timer
    exports.Set("clearInterval", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!args.empty() && args[0].IsNumber())
        {
            Lode::Task::ClearInterval(vm, static_cast<int>(args[0].AsNumber()));
        }
        return Lode::Value();
    }));

    // Secondary MetaInfo table
    Lode::Table metaInfo = vm.CreateTable();
    metaInfo.Set("moduleName", Lode::Value("TaskModule"));
    metaInfo.Set("version", Lode::Value("1.0.0"));

    return { exports, metaInfo };
}
