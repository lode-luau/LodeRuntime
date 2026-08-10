// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tty/TtyManager.hpp"
#include "Tty/TtyStream.hpp"
#include "Tty/TtyHelpers.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"

namespace
{

Lode::Table BuildStreamMethods(Lode::State& vm, const std::shared_ptr<lodetty::TtyManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Write", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty Write: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodWrite(vm2, args);
    }));

    m.Set("WriteLine", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty WriteLine: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodWriteLine(vm2, args);
    }));

    m.Set("Read", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty Read: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodRead(vm2, args);
    }));

    m.Set("ReadBuffer", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty ReadBuffer: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodReadBuffer(vm2, args);
    }));

    m.Set("ReadLine", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty ReadLine: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodReadLine(vm2, args);
    }));

    m.Set("StartStreaming", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty StartStreaming: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodStartStreaming(vm2);
    }));

    m.Set("StopStreaming", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty StopStreaming: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodStopStreaming(vm2);
    }));

    m.Set("GetWindowSize", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty GetWindowSize: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodGetWindowSize(vm2);
    }));

    m.Set("SetMode", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty SetMode: invalid TtyStream");
            return Lode::Value();
        }
        return self->MethodSetMode(vm2, args);
    }));

    m.Set("IsOpen", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->open && !self->closing && !self->closed);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty Close: invalid TtyStream");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodetty::TtyStream>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("tty Destroy: invalid TtyStream");
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
    auto mgr = std::make_shared<lodetty::TtyManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->streamMethods = BuildStreamMethods(vm, mgr);

    Lode::Table streamClass = vm.CreateTable();
    streamClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("tty: runtime is shutting down");
            return Lode::Value();
        }
        if (args.size() < 3 || !args[1].IsNumber() || !args[2].IsBoolean())
        {
            vm2.RaiseError("tty Create: expected fd (number) and readable (boolean)");
            return Lode::Value();
        }
        auto fd = Lode::Numeric::ToInt64(args[1].AsNumber(), "fd");
        if (fd.IsError())
        {
            vm2.RaiseError(fd.GetError().ErrorMessage());
            return Lode::Value();
        }
        auto stream = std::make_shared<lodetty::TtyStream>();
        stream->mgr = mgr;
        stream->mainL = mgr->mainL;
        stream->loop = mgr->loop;
        std::string err = stream->InitNative(static_cast<int>(fd.GetValue()), args[2].AsBoolean());
        if (!err.empty())
        {
            vm2.RaiseError("tty Create: " + err);
            return Lode::Value();
        }
        stream->InitSignals(vm2);
        mgr->AddStream(stream);
        stream->selfGuard = stream;
        return lodetty::WrapTtyStream(vm2, stream, mgr->streamMethods);
    }));

    Lode::Exports exports(vm);

    exports.SetTable("TtyStream", streamClass);

    exports.Function("IsTTY", [](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 1 || !args[0].IsNumber())
        {
            vm2.RaiseError("tty.IsTTY: fd must be a number");
            return Lode::Value();
        }
        auto fd = Lode::Numeric::ToInt64(args[0].AsNumber(), "fd");
        if (fd.IsError())
        {
            vm2.RaiseError(fd.GetError().ErrorMessage());
            return Lode::Value();
        }
        return Lode::Value(lodetty::IsTtyFd(static_cast<int>(fd.GetValue())));
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}


