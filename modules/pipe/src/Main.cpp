// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Pipe/PipeManager.hpp"
#include "Pipe/PipeStream.hpp"
#include "Pipe/PipeServer.hpp"
#include "Pipe/PipeHelpers.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"

namespace
{

Lode::Table BuildStreamMethods(Lode::State& vm, const std::shared_ptr<lodepipe::PipeManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Connect", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Connect: invalid PipeStream");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe Connect: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodConnect(vm2, args);
    }));

    m.Set("OpenFD", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe OpenFD: invalid PipeStream");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe OpenFD: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodOpenFD(vm2, args);
    }));

    m.Set("Write", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Write: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodWrite(vm2, args);
    }));

    m.Set("WriteLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe WriteLine: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodWriteLine(vm2, args);
    }));

    m.Set("Read", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Read: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodRead(vm2, args);
    }));

    m.Set("ReadBuffer", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe ReadBuffer: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodReadBuffer(vm2, args);
    }));

    m.Set("ReadLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe ReadLine: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodReadLine(vm2, args);
    }));

    m.Set("StartStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe StartStreaming: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodStartStreaming(vm2);
    }));

    m.Set("StopStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe StopStreaming: invalid PipeStream");
            return Lode::Value();
        }
        return self->MethodStopStreaming(vm2);
    }));

    m.Set("IsOpen", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->open && !self->closing && !self->closed);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Close: invalid PipeStream");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Destroy: invalid PipeStream");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    return m;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<lodepipe::PipeManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Server: invalid PipeServer");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Accept", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Server: invalid PipeServer");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodAccept(vm2);
    }));

    m.Set("IsListening", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeServer>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Server: invalid PipeServer");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodepipe::PipeServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("pipe Server: invalid PipeServer");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    return m;
}

} // namespace

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodepipe::PipeManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->streamMethods = BuildStreamMethods(vm, mgr);
    mgr->serverMethods = BuildServerMethods(vm, mgr);

    Lode::Table streamClass = vm.CreateTable();
    streamClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe: runtime is shutting down");
            return Lode::Value();
        }
        auto stream = std::make_shared<lodepipe::PipeStream>();
        stream->mgr = mgr;
        stream->mainL = mgr->mainL;
        stream->loop = mgr->loop;
        stream->InitSignals(vm2);
        mgr->AddStream(stream);
        stream->selfGuard = stream;
        return lodepipe::WrapPipeStream(vm2, stream, mgr->streamMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("pipe: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<lodepipe::PipeServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        server->InitSignals(vm2);
        mgr->AddServer(server);
        server->selfGuard = server;
        return lodepipe::WrapPipeServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);

    exports.SetTable("PipeStream", streamClass);
    exports.SetTable("PipeServer", serverClass);

    exports.Function("IsPipe", [](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsNumber())
        {
            vm2.RaiseError("pipe.IsPipe: fd must be a number");
            return Lode::Value();
        }
        auto fd = Lode::Numeric::ToInt64(args[0].AsNumber(), "fd");
        if (fd.IsError())
        {
            vm2.RaiseError(fd.GetError().ErrorMessage());
            return Lode::Value();
        }
        return Lode::Value(lodepipe::IsPipeFd(static_cast<int>(fd.GetValue())));
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}