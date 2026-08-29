// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Udp/UdpSocket.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"
#include <algorithm>

namespace
{

struct UdpManager
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;
    Lode::Table socketMethods;
    std::vector<std::weak_ptr<lodeudp::UdpSocket>> sockets;

    void AddSocket(const std::shared_ptr<lodeudp::UdpSocket>& s)
    {
        // Prune expired entries so long-lived processes that create and close
        // many sockets do not grow this vector forever.
        sockets.erase(
            std::remove_if(sockets.begin(), sockets.end(),
                [](const std::weak_ptr<lodeudp::UdpSocket>& w) { return w.expired(); }),
            sockets.end());
        sockets.push_back(s);
    }
    void Shutdown()
    {
        shuttingDown = true;
        auto copy = sockets;
        for (auto& w : copy) {
            if (auto s = w.lock()) {
                s->shuttingDown = true;
                s->RequestClose();
            }
        }
    }
};

Lode::Table BuildSocketMethods(Lode::State& vm, const std::shared_ptr<UdpManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Bind", vm.CreateFastFunctionNoYield([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("udp Bind: invalid UdpSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("udp Bind: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodBind(vm2, args);
    }));

    m.Set("Send", vm.CreateFastFunctionNoYield([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("udp Send: invalid UdpSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("udp Send: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodSend(vm2, args);
    }));

    m.Set("Close", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("udp Close: invalid UdpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("udp Destroy: invalid UdpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("JoinGroup", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("udp JoinGroup: invalid UdpSocket"); return Lode::Value(); }
        return self->MethodJoinGroup(vm2, args);
    }));

    m.Set("LeaveGroup", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("udp LeaveGroup: invalid UdpSocket"); return Lode::Value(); }
        return self->MethodLeaveGroup(vm2, args);
    }));

    m.Set("SetBroadcast", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("udp SetBroadcast: invalid UdpSocket"); return Lode::Value(); }
        return self->MethodSetBroadcast(vm2, args);
    }));

    m.Set("SetTTL", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("udp SetTTL: invalid UdpSocket"); return Lode::Value(); }
        return self->MethodSetTTL(vm2, args);
    }));

    m.Set("SetMulticastLoop", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("udp SetMulticastLoop: invalid UdpSocket"); return Lode::Value(); }
        return self->MethodSetMulticastLoop(vm2, args);
    }));

    m.Set("LocalAddress", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodeudp::UdpSocket>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("udp LocalAddress: invalid UdpSocket");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<UdpManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->socketMethods = BuildSocketMethods(vm, mgr);

    Lode::Table udpSocketClass = vm.CreateTable();
    udpSocketClass.Set("Create", vm.CreateFastFunctionNoYield([mgr](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("udp: runtime is shutting down");
            return Lode::Value();
        }
        auto socket = std::make_shared<lodeudp::UdpSocket>();
        socket->mainL = mgr->mainL;
        socket->loop = mgr->loop;
        socket->InitSignals(vm2);
        mgr->AddSocket(socket);
        socket->selfGuard = socket;
        return lodeudp::WrapUdpSocket(vm2, socket, mgr->socketMethods);
    }));

    Lode::Exports exports(vm);
    exports.SetTable("UdpSocket", udpSocketClass);
    return Lode::ModuleReturn(exports.GetExportTable());
}
