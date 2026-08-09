// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Signal.hpp"
#include "Lode/Task.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include <map>
#include <string>
#include <utility>

namespace Lode
{
namespace
{
    // Unhandled errors from resumed Wait continuations are reported the same
    // way Task::Spawn and the timer path report async task failures.
    void EmitSignalError(const std::string& message)
    {
        Diagnostic diag;
        diag.message = message;
        diag.code = "TaskError";
        Logger::EmitDiagnostic(diag);
    }

    // Payload stored inside the read-only Connection userdata exposed to Luau.
    // It keeps the signal alive (a connection references its signal, exactly
    // like modules/signal) and remembers the connection id for Disconnect.
    struct ConnectionHandle
    {
        std::shared_ptr<Signal> signal;
        uint64_t id = 0;
    };
} // namespace

struct Signal::Connection
{
    uint64_t id = 0;
    std::vector<Value> callback;
    bool once = false;
    bool connected = true;
    std::shared_ptr<Waiter> waiter; // non-null only for Wait connections
};

// State for a pending Signal:Wait. The pinned Coroutine keeps the waiting
// thread alive until the signal fires or the timeout elapses.
struct Signal::Waiter
{
    Coroutine thread;
    bool withTimeout = false;
    int timerId = -1;
    bool resumed = false; // guards against a double schedule (Fire vs timeout)
};

struct Signal::Impl
{
    lua_State* mainL = nullptr;
    bool destroyed = false;
    uint64_t nextId = 1;
    std::map<uint64_t, std::shared_ptr<Connection>> connections;
};

Signal::Signal(State& vm) : impl_(std::make_unique<Impl>())
{
    impl_->mainL = vm.GetMainThread();
}

Signal::~Signal()
{
    if (impl_)
        Shutdown();
}

std::shared_ptr<Signal> Signal::Create(State& vm)
{
    auto signal = std::shared_ptr<Signal>(new Signal(vm));
    std::weak_ptr<Signal> weak = signal;
    Task::RegisterShutdownHook(vm, [weak]() {
        if (auto locked = weak.lock())
            locked->Shutdown();
    });
    return signal;
}

uint64_t Signal::Connect(const Value& callback)
{
    return AddConnection(callback, false);
}

uint64_t Signal::Once(const Value& callback)
{
    return AddConnection(callback, true);
}

uint64_t Signal::AddConnection(const Value& callback, bool once)
{
    if (impl_->destroyed)
    {
        State vm(impl_->mainL);
        vm.RaiseError("Signal is destroyed");
        return 0;
    }
    if (!callback.IsFunction())
    {
        State vm(impl_->mainL);
        vm.RaiseError("Signal:Connect expects a function callback");
        return 0;
    }

    auto conn = std::make_shared<Connection>();
    conn->id = impl_->nextId++;
    conn->once = once;
    conn->callback.push_back(callback);
    impl_->connections[conn->id] = conn;
    return conn->id;
}

void Signal::Disconnect(uint64_t connectionId)
{
    auto it = impl_->connections.find(connectionId);
    if (it == impl_->connections.end())
        return;
    auto conn = it->second;
    if (!conn->connected)
        return;

    conn->connected = false;
    if (conn->waiter && conn->waiter->withTimeout && conn->waiter->timerId >= 0)
    {
        State vm(impl_->mainL);
        Task::ClearTimeout(vm, conn->waiter->timerId);
    }
    impl_->connections.erase(it);
}

bool Signal::IsConnected(uint64_t connectionId) const
{
    auto it = impl_->connections.find(connectionId);
    return it != impl_->connections.end() && it->second->connected;
}

size_t Signal::ConnectionCount() const
{
    return impl_->connections.size();
}

void Signal::DisconnectAll()
{
    if (impl_->destroyed)
        return;
    State vm(impl_->mainL);
    for (auto& [id, conn] : impl_->connections)
    {
        (void)id;
        if (conn->waiter && conn->waiter->withTimeout && conn->waiter->timerId >= 0)
            Task::ClearTimeout(vm, conn->waiter->timerId);
    }
    impl_->connections.clear();
}

void Signal::Fire()
{
    Fire(std::vector<Value>{});
}

void Signal::Fire(const Value& arg)
{
    Fire(std::vector<Value>{arg});
}

void Signal::Fire(const std::vector<Value>& args)
{
    if (impl_->destroyed)
        return;

    State vm(impl_->mainL);

    // Snapshot the live connections so callbacks that connect or disconnect
    // during a Fire do not invalidate the iteration, and firing order stays
    // deterministic (connection order).
    std::vector<std::shared_ptr<Connection>> snapshot;
    snapshot.reserve(impl_->connections.size());
    for (auto& [id, conn] : impl_->connections)
    {
        (void)id;
        if (conn->connected)
            snapshot.push_back(conn);
    }

    for (auto& conn : snapshot)
    {
        if (!conn->connected)
            continue;

        if (conn->waiter)
        {
            // A Wait connection: hand the fire arguments to the waiting
            // thread on the next tick, mirroring modules/signal's task.defer.
            conn->connected = false;
            if (conn->waiter->withTimeout && conn->waiter->timerId >= 0)
                Task::ClearTimeout(vm, conn->waiter->timerId);
            impl_->connections.erase(conn->id);
            if (!conn->waiter->resumed)
            {
                conn->waiter->resumed = true;
                Task::Defer(vm, MakeResumeClosure(vm, conn->waiter, args));
            }
            continue;
        }

        if (conn->once)
        {
            // Disconnect before spawning so a self-reconnecting or recursive
            // callback cannot cause a once connection to fire twice.
            conn->connected = false;
            impl_->connections.erase(conn->id);
        }
        Task::Spawn(vm, conn->callback.front(), args);
    }
}

Value Signal::MakeResumeClosure(State& vm, const std::shared_ptr<Waiter>& waiter, const std::vector<Value>& args)
{
    return vm.CreateFunction([waiter, args](State& vm2, const std::vector<Value>&) -> Value {
        auto res = waiter->thread.Resume(args);
        if (res.IsError())
        {
            if (Task::IsMainThread(vm2, waiter->thread.GetThreadState()))
                Task::SetMainThreadError(vm2, res.GetError().ErrorMessage());
            else
                EmitSignalError("Unhandled exception in Signal:Wait continuation: " + res.GetError().ErrorMessage());
        }
        return Value();
    });
}

int Signal::Wait(State& vm, const std::vector<Value>& args)
{
    if (impl_->destroyed)
    {
        vm.RaiseError("Signal is destroyed");
        return 0;
    }

    bool withTimeout = false;
    double timeoutSeconds = 0.0;
    if (!args.empty() && !args[0].IsNil())
    {
        if (!args[0].IsNumber())
        {
            vm.RaiseError("Signal:Wait timeout must be a number or nil");
            return 0;
        }
        withTimeout = true;
        timeoutSeconds = args[0].AsNumber();
    }

    // The waiter connection is registered before yielding, so a Fire that
    // happens during the yield is guaranteed to see it.
    auto waiter = std::make_shared<Waiter>();
    waiter->thread = Coroutine(vm.GetLuaState());
    waiter->withTimeout = withTimeout;

    auto conn = std::make_shared<Connection>();
    conn->id = impl_->nextId++;
    conn->once = true;
    conn->waiter = waiter;
    impl_->connections[conn->id] = conn;

    if (withTimeout)
    {
        auto ms = Numeric::ToMilliseconds(timeoutSeconds, 1000.0, "Signal:Wait timeout");
        if (ms.IsError())
        {
            impl_->connections.erase(conn->id);
            vm.RaiseError(ms.GetError().ErrorMessage());
            return 0;
        }

        uint64_t connectionId = conn->id;
        // The timeout closure keeps the signal alive until it fires, so a
        // pending timeout can always disconnect its waiter even if every other
        // reference is dropped.
        auto strong = shared_from_this();
        auto timeoutClosure = vm.CreateFunction([strong, waiter, connectionId](State& vm2, const std::vector<Value>&) -> Value {
            if (waiter->resumed)
                return Value();
            waiter->resumed = true;
            strong->Disconnect(connectionId);
            Task::Defer(vm2, strong->MakeResumeClosure(vm2, waiter, {}));
            return Value();
        });
        waiter->timerId = Task::SetTimeout(vm, timeoutClosure, static_cast<double>(ms.GetValue()));
    }

    return vm.YieldThread();
}

Value Signal::CreatePublic()
{
    if (impl_->destroyed)
    {
        State vm(impl_->mainL);
        vm.RaiseError("Signal is destroyed");
        return Value();
    }

    State vm(impl_->mainL);
    Table proxy = vm.CreateTable();
    auto strong = shared_from_this();

    proxy.Set("Connect", vm.CreateFunction([strong](State& vm2, const std::vector<Value>& args) -> Value {
        // args[0] is the proxy (method-call self), args[1] is the callback.
        if (args.size() < 2 || !args[1].IsFunction())
        {
            vm2.RaiseError("signal:Connect expects a function callback");
            return Value();
        }
        uint64_t id = strong->Connect(args[1]);
        return strong->WrapConnection(vm2, id);
    }));

    proxy.Set("Once", vm.CreateFunction([strong](State& vm2, const std::vector<Value>& args) -> Value {
        // args[0] is the proxy (method-call self), args[1] is the callback.
        if (args.size() < 2 || !args[1].IsFunction())
        {
            vm2.RaiseError("signal:Once expects a function callback");
            return Value();
        }
        uint64_t id = strong->Once(args[1]);
        return strong->WrapConnection(vm2, id);
    }));

    proxy.Set("Wait", vm.CreateFunction([strong](State& vm2, const std::vector<Value>& args) -> Value {
        // args[0] is the proxy (method-call self); pass the remaining args
        // (the optional timeout) to the Wait implementation.
        std::vector<Value> waitArgs;
        if (args.size() > 1)
            waitArgs.assign(args.begin() + 1, args.end());
        return Value(strong->Wait(vm2, waitArgs));
    }));

    vm.FreezeTable(proxy);
    return Value(proxy);
}

Value Signal::WrapConnection(State& vm, uint64_t connectionId)
{
    auto handle = std::make_shared<ConnectionHandle>();
    handle->signal = shared_from_this();
    handle->id = connectionId;

    Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([handle](State& vm2, const std::vector<Value>& args) -> Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "Disconnect")
        {
            return vm2.CreateFunction([handle](State&, const std::vector<Value>&) -> Value {
                if (handle->signal)
                    handle->signal->Disconnect(handle->id);
                return Value();
            });
        }
        if (key == "Connected")
            return Value(handle->signal && handle->signal->IsConnected(handle->id));
        return Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](State& vm2, const std::vector<Value>&) -> Value {
        vm2.RaiseError("signal connection is read-only");
        return Value();
    }));
    meta.Set("__metatable", Value(std::string("connection")));
    meta.Set("__tostring", vm.CreateFunction([](State&, const std::vector<Value>&) -> Value {
        return Value(std::string("connection"));
    }));

    ObjectWrap<ConnectionHandle>::Wrap(vm, handle, meta);
    Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

void Signal::Shutdown()
{
    if (impl_->destroyed)
        return;
    impl_->destroyed = true;
    State vm(impl_->mainL);
    for (auto& [id, conn] : impl_->connections)
    {
        (void)id;
        if (conn->waiter && conn->waiter->withTimeout && conn->waiter->timerId >= 0)
            Task::ClearTimeout(vm, conn->waiter->timerId);
    }
    impl_->connections.clear();
}

} // namespace Lode
