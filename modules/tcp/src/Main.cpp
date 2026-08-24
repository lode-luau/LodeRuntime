// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tcp/TcpManager.hpp"
#include "Tcp/TcpClient.hpp"
#include "Tcp/TcpServer.hpp"
#include "Tcp/Resolve.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"

namespace
{

Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<lodetcp::TcpManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Connect", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Connect: invalid TcpSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket Connect: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodConnect(vm2, args);
    }));

    m.Set("Send", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Send: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodSend(vm2, args);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Close: invalid TcpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Destroy: invalid TcpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    // Half-close: send FIN, keep reading until the remote closes.
    m.Set("End", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket End: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodEnd(vm2);
    }));

    m.Set("IsConnected", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->connected && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket LocalAddress: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    m.Set("RemoteAddress", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket RemoteAddress: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodRemoteAddress(vm2);
    }));

    m.Set("SetNoDelay", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket SetNoDelay: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodSetNoDelay(vm2, args);
    }));

    m.Set("SetKeepAlive", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket SetKeepAlive: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodSetKeepAlive(vm2, args);
    }));

    return m;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<lodetcp::TcpManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsListening", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpServer>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetcp::TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodetcp::TcpManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->clientMethods = BuildClientMethods(vm, mgr);
    mgr->serverMethods = BuildServerMethods(vm, mgr);

    Lode::Table tcpSocketClass = vm.CreateTable();
    tcpSocketClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket: runtime is shutting down");
            return Lode::Value();
        }
        auto client = std::make_shared<lodetcp::TcpClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        return lodetcp::WrapClient(vm2, client, mgr->clientMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<lodetcp::TcpServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        if (args.Size() > 1 && !args[1].IsNil())
        {
            if (!args[1].IsTable())
            {
                vm2.RaiseError("socket.Server:Create: opts must be a table or nil");
                return Lode::Value();
            }
            auto backlog = args[1].AsTable().Get("backlog");
            if (backlog.IsOk() && !backlog.GetValue().IsNil())
            {
                if (!backlog.GetValue().IsNumber())
                {
                    vm2.RaiseError("socket.Server:Create: backlog must be a number");
                    return Lode::Value();
                }
                double value = backlog.GetValue().AsNumber();
                bool valid = true;
                if (value < 1 || value > 65535) valid = false;
                if (!valid)
                {
                    vm2.RaiseError("socket.Server:Create: backlog must be an integer between 1 and 65535");
                    return Lode::Value();
                }
                server->backlog = static_cast<int>(value);
            }
        }
        server->InitSignals(vm2);
        mgr->AddServer(server);
        server->selfGuard = server;
        return lodetcp::WrapServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);

    exports.SetTable("Server", serverClass);
    exports.SetTable("TcpSocket", tcpSocketClass);

    exports.Function("resolve", [mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString())
        {
            vm2.RaiseError("socket.resolve: host must be a string");
            return Lode::Value();
        }
        std::string host = args[0].AsString();
        if (host.empty())
        {
            vm2.RaiseError("socket.resolve: host must not be empty");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket.resolve: runtime is shutting down");
            return Lode::Value();
        }
        int r = lodetcp::StartResolve(vm2, mgr, host, Lode::Coroutine(vm2.GetLuaState()), Lode::Value());
        if (r != 0)
        {
            vm2.RaiseError("socket.resolve: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return vm2.YieldThread();
    });

    exports.Function("resolveAsync", [mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsString() || !args[1].IsFunction())
        {
            vm2.RaiseError("socket.resolveAsync: expected host and callback");
            return Lode::Value();
        }
        std::string host = args[0].AsString();
        if (host.empty())
        {
            vm2.RaiseError("socket.resolveAsync: host must not be empty");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket.resolveAsync: runtime is shutting down");
            return Lode::Value();
        }
        int r = lodetcp::StartResolve(vm2, mgr, host, Lode::Coroutine(), args[1].ToValue());
        if (r != 0)
        {
            vm2.RaiseError("socket.resolveAsync: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return Lode::Value();
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}
