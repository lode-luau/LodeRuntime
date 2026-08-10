// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "WebSocket/WebSocketExport.hpp"
#include "WebSocket/WebSocketManager.hpp"
#include "WebSocket/WsClient.hpp"
#include "WebSocket/WsServer.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"

namespace lodews
{

Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<WebSocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Connect", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Connect: invalid WebSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket Connect: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodConnect(vm2, args);
    }));

    m.Set("Send", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Send: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodSend(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Close: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodClose(vm2, args);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket LocalAddress: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    m.Set("RemoteAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket RemoteAddress: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodRemoteAddress(vm2);
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Destroy: invalid WebSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsConnected", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->state == WsState::Open && !self->closed);
    }));

    m.Set("ReadyState", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(std::string("closed"));
        switch (self->state)
        {
            case WsState::Connecting: return Lode::Value(std::string("connecting"));
            case WsState::Open:       return Lode::Value(std::string("open"));
            case WsState::Closing:    return Lode::Value(std::string("closing"));
            case WsState::Closed:     return Lode::Value(std::string("closed"));
        }
        return Lode::Value(std::string("closed"));
    }));

    return m;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<WebSocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsListening", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace lodews

LODE_MODULE(vm)
{
    using namespace lodews;
    auto mgr = std::make_shared<WebSocketManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    mgr->rng.Seed(uv_hrtime());
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->clientMethods = BuildClientMethods(vm, mgr);
    mgr->serverMethods = BuildServerMethods(vm, mgr);

    Lode::Table wsClass = vm.CreateTable();
    wsClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket: runtime is shutting down");
            return Lode::Value();
        }
        auto client = std::make_shared<WsClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        return WrapClient(vm2, client, mgr->clientMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<WsServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        
        // Note: The original backlog initialization was checking backlog port instead of backlog integer. 
        // Oh wait, in original it did `if (!IsValidPort(value))`, but backlog isn't a port! It was just checking if it's an integer.
        // It's fine to leave it out or correctly check integer.
        int backlog = 511;
        if (args.size() > 1 && !args[1].IsNil())
        {
            if (!args[1].IsTable())
            {
                vm2.RaiseError("websocket.WebSocketServer:Create: opts must be a table or nil");
                return Lode::Value();
            }
            auto backlogVal = args[1].AsTable().Get("backlog");
            if (backlogVal.IsOk() && !backlogVal.GetValue().IsNil())
            {
                double value = backlogVal.GetValue().AsNumber();
                if (!std::isfinite(value) || value < 1)
                {
                    vm2.RaiseError("websocket.WebSocketServer:Create: backlog must be an integer > 0");
                    return Lode::Value();
                }
                server->tcpServer = std::make_shared<lodetcp::TcpServer>();
                server->tcpServer->backlog = static_cast<int>(value);
            }
        }
        server->InitSignals(vm2);
        mgr->AddServer(server);
        server->selfGuard = server;
        return WrapServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);
    exports.SetTable("WebSocket", wsClass);
    exports.SetTable("WebSocketServer", serverClass);

    return Lode::ModuleReturn(exports.GetExportTable());
}
