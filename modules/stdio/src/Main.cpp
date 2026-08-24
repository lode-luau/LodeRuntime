// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Stdio/StdioManager.hpp"
#include "Stdio/StdioStream.hpp"
#include "Stdio/StdioHelpers.hpp"
#include "Lode/Module.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

Lode::Table BuildStreamMethods(Lode::State& vm, const std::shared_ptr<lodestdio::StdioManager>&)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Write", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio Write: invalid StdioStream"); return Lode::Value(); }
        return self->MethodWrite(vm2, args);
    }));

    m.Set("WriteLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio WriteLine: invalid StdioStream"); return Lode::Value(); }
        return self->MethodWriteLine(vm2, args);
    }));

    m.Set("Read", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio Read: invalid StdioStream"); return Lode::Value(); }
        return self->MethodRead(vm2, args);
    }));

    m.Set("ReadBuffer", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadBuffer: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadBuffer(vm2, args);
    }));

    m.Set("ReadLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadLine: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadLine(vm2, args);
    }));

    m.Set("ReadInto", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadInto: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadInto(vm2, args);
    }));

    m.Set("ReadAsync", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadAsync: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadAsync(vm2, args);
    }));

    m.Set("ReadBufferAsync", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadBufferAsync: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadBufferAsync(vm2, args);
    }));

    m.Set("ReadIntoAsync", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio ReadIntoAsync: invalid StdioStream"); return Lode::Value(); }
        return self->MethodReadIntoAsync(vm2, args);
    }));

    m.Set("StartStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio StartStreaming: invalid StdioStream"); return Lode::Value(); }
        return self->MethodStartStreaming(vm2);
    }));

    m.Set("StopStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio StopStreaming: invalid StdioStream"); return Lode::Value(); }
        return self->MethodStopStreaming(vm2);
    }));

    m.Set("GetWindowSize", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio GetWindowSize: invalid StdioStream"); return Lode::Value(); }
        return self->MethodGetWindowSize(vm2);
    }));

    m.Set("SetMode", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio SetMode: invalid StdioStream"); return Lode::Value(); }
        return self->MethodSetMode(vm2, args);
    }));

    m.Set("SetRawMode", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio SetRawMode: invalid StdioStream"); return Lode::Value(); }
        return self->MethodSetRawMode(vm2, args);
    }));

    m.Set("IsTTY", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(false);
        return self->MethodIsTTY(vm2);
    }));

    m.Set("IsOpen", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(false);
        return Lode::Value(self->open && !self->closing && !self->closed);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio Close: invalid StdioStream"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<lodestdio::StdioStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("stdio Destroy: invalid StdioStream"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    // camelCase aliases for backward compatibility
    m.Set("write", m.Get("Write").GetValue());
    m.Set("writeLine", m.Get("WriteLine").GetValue());
    m.Set("read", m.Get("Read").GetValue());
    m.Set("readBuffer", m.Get("ReadBuffer").GetValue());
    m.Set("readLine", m.Get("ReadLine").GetValue());
    m.Set("readInto", m.Get("ReadInto").GetValue());
    m.Set("readAsync", m.Get("ReadAsync").GetValue());
    m.Set("readBufferAsync", m.Get("ReadBufferAsync").GetValue());
    m.Set("readIntoAsync", m.Get("ReadIntoAsync").GetValue());
    m.Set("isTTY", m.Get("IsTTY").GetValue());
    m.Set("setRawMode", m.Get("SetRawMode").GetValue());
    m.Set("getWindowSize", m.Get("GetWindowSize").GetValue());

    return m;
}

} // namespace

// Formats a number the way Luau's tostring() does: the shortest decimal
// representation that round-trips back to the same double, so
// stdio.print(0.1) renders "0.1" instead of std::to_string's "0.100000".
static const char* FormatLuauNumber(double val)
{
    static thread_local char buf[40];
    if (val == static_cast<int64_t>(val) && std::fabs(val) < 1e15)
    {
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(static_cast<int64_t>(val)));
        return buf;
    }
    for (int prec = 15; prec <= 17; ++prec)
    {
        snprintf(buf, sizeof(buf), "%.*g", prec, val);
        if (strtod(buf, nullptr) == val)
            return buf;
    }
    snprintf(buf, sizeof(buf), "%.17g", val);
    return buf;
}

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<lodestdio::StdioManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->streamMethods = BuildStreamMethods(vm, mgr);

    auto createStream = [mgr](Lode::State& vm2, int fd, bool readable) -> std::shared_ptr<lodestdio::StdioStream> {
        auto s = std::make_shared<lodestdio::StdioStream>();
        s->mgr = mgr;
        s->mainL = mgr->mainL;
        s->loop = mgr->loop;
        (void)s->InitNative(fd, readable);
        s->InitSignals(vm2);
        mgr->AddStream(s);
        s->selfGuard = s;
        return s;
    };

    mgr->stdinStream = createStream(vm, 0, true);
    mgr->stdoutStream = createStream(vm, 1, false);
    mgr->stderrStream = createStream(vm, 2, false);

    Lode::Value stdinVal = lodestdio::WrapStdioStream(vm, mgr->stdinStream, mgr->streamMethods);
    Lode::Value stdoutVal = lodestdio::WrapStdioStream(vm, mgr->stdoutStream, mgr->streamMethods);
    Lode::Value stderrVal = lodestdio::WrapStdioStream(vm, mgr->stderrStream, mgr->streamMethods);

    Lode::Table streamClass = vm.CreateTable();
    streamClass.Set("Create", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("stdio: runtime is shutting down");
            return Lode::Value();
        }
        if (args.Size() < 3 || !args[1].IsNumber() || !args[2].IsBoolean())
        {
            vm2.RaiseError("stdio Create: expected fd (number) and readable (boolean)");
            return Lode::Value();
        }
        auto fd = Lode::Numeric::ToInt64(args[1].AsNumber(), "fd");
        if (fd.IsError())
        {
            vm2.RaiseError(fd.GetError().ErrorMessage());
            return Lode::Value();
        }
        auto s = std::make_shared<lodestdio::StdioStream>();
        s->mgr = mgr;
        s->mainL = mgr->mainL;
        s->loop = mgr->loop;
        std::string err = s->InitNative(static_cast<int>(fd.GetValue()), args[2].AsBoolean());
        if (!err.empty())
        {
            vm2.RaiseError("stdio Create: " + err);
            return Lode::Value();
        }
        s->InitSignals(vm2);
        mgr->AddStream(s);
        s->selfGuard = s;
        return lodestdio::WrapStdioStream(vm2, s, mgr->streamMethods);
    }));

    Lode::Table exports = vm.CreateTable();

    exports.Set("stdin", stdinVal);
    exports.Set("stdout", stdoutVal);
    exports.Set("stderr", stderrVal);
    exports.Set("StdioStream", Lode::Value(streamClass));

    auto stdoutStream = mgr->stdoutStream;
    auto stderrStream = mgr->stderrStream;
    auto stdinStream = mgr->stdinStream;

    exports.Set("print", vm.CreateFastFunction([stdoutStream](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (!stdoutStream || !stdoutStream->open) { vm2.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        std::string out;
        for (size_t i = 0; i < args.Size(); ++i) {
            if (i > 0) out += " ";
            if (args[i].IsString()) out += args[i].AsStringView();
            else if (args[i].IsNumber()) out += FormatLuauNumber(args[i].AsNumber());
            else if (args[i].IsBoolean()) out += args[i].AsBoolean() ? "true" : "false";
            else out += "[Value]";
        }
        out += "\n";
        stdoutStream->WriteNative(out.data(), out.size());
        return Lode::Value();
    }));

    exports.Set("eprint", vm.CreateFastFunction([stderrStream](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (!stderrStream || !stderrStream->open) { vm2.RaiseError("stdio stderr is unavailable"); return Lode::Value(); }
        std::string out;
        for (size_t i = 0; i < args.Size(); ++i) {
            if (i > 0) out += " ";
            if (args[i].IsString()) out += args[i].AsStringView();
            else if (args[i].IsNumber()) out += FormatLuauNumber(args[i].AsNumber());
            else if (args[i].IsBoolean()) out += args[i].AsBoolean() ? "true" : "false";
            else out += "[Value]";
        }
        out += "\n";
        stderrStream->WriteNative(out.data(), out.size());
        return Lode::Value();
    }));

    exports.Set("write", vm.CreateFastFunction([stdoutStream](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (!stdoutStream || !stdoutStream->open) { vm2.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        if (args.Size() > 0) {
            if (args[0].IsString()) {
                std::string_view sv = args[0].AsStringView();
                stdoutStream->WriteNative(sv.data(), sv.size());
            } else if (args[0].IsBuffer()) {
                size_t sz = 0;
                void* ptr = args[0].AsBuffer(&sz);
                if (ptr) stdoutStream->WriteNative(static_cast<const char*>(ptr), sz);
            }
        }
        return Lode::Value();
    }));

    exports.Set("prompt", vm.CreateFastFunction([stdinStream, stdoutStream](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (!stdinStream || !stdinStream->open) { vm2.RaiseError("stdio stdin is unavailable"); return Lode::Value(); }
        if (args.Size() > 0 && args[0].IsString()) {
            std::string_view sv = args[0].AsStringView();
            if (stdoutStream && stdoutStream->open)
                stdoutStream->WriteNative(sv.data(), sv.size());
        }
        stdinStream->mainL = vm2.GetMainThread();
        lodestdio::StdioStream::PendingRead req;
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm2.GetLuaState());
        req.isLine = true;
        stdinStream->QueueRequest(req);
        vm2.YieldThread();
        return Lode::Value();
    }));

    exports.Set("clear", vm.CreateFastFunction([stdoutStream](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        if (!stdoutStream || !stdoutStream->open) { vm2.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        const char* clearCmd = "\033[2J\033[H";
        stdoutStream->WriteNative(clearCmd, std::strlen(clearCmd));
        return Lode::Value();
    }));

    exports.Set("IsTTY", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsNumber())
        {
            vm2.RaiseError("stdio.IsTTY: fd must be a number");
            return Lode::Value();
        }
        auto fd = Lode::Numeric::ToInt64(args[0].AsNumber(), "fd");
        if (fd.IsError())
        {
            vm2.RaiseError(fd.GetError().ErrorMessage());
            return Lode::Value();
        }
        return Lode::Value(lodestdio::IsTtyFd(static_cast<int>(fd.GetValue())));
    }));

    exports.Set("isTTY", exports.Get("IsTTY").GetValue());

    exports.Set("select", vm.CreateFastFunction([stdinStream, stdoutStream](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        if (!stdinStream || !stdoutStream || stdinStream->handleType != UV_TTY || stdoutStream->handleType != UV_TTY)
        {
            vm2.RaiseError("stdio.select requires a TTY terminal");
            return Lode::Value();
        }
        if (args.Size() < 2 || !args[0].IsString() || !args[1].IsTable())
        {
            vm2.RaiseError("stdio.select: expected (prompt: string, options: {string})");
            return Lode::Value();
        }
        std::string prompt = args[0].AsString();
        Lode::Table optTable = args[1].AsTable();
        std::vector<std::string> options;
        int idx = 1;
        while (true)
        {
            auto item = optTable.Get(idx);
            if (item.IsError() || !item.GetValue().IsString())
                break;
            options.push_back(item.GetValue().AsString());
            idx++;
        }
        if (options.empty())
        {
            Lode::Table ret = vm2.CreateTable();
            ret.Set(1, Lode::Value(0.0));
            ret.Set(2, Lode::Value(std::string("")));
            return Lode::Value(ret);
        }

        uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(&stdinStream->ttyHandle), UV_TTY_MODE_RAW);
        stdoutStream->WriteNative("\x1B[?25l", 6);

        vm2.YieldThread();
        Lode::Coroutine coro(vm2.GetLuaState());
        auto selectedIdx = std::make_shared<size_t>(0);

        auto render = [stdoutStream, prompt, options, selectedIdx]() {
            std::string out = prompt + "\r\n";
            for (size_t i = 0; i < options.size(); ++i)
            {
                if (i == *selectedIdx)
                    out += "\x1B[36m> " + options[i] + "\x1B[0m\r\n";
                else
                    out += "  " + options[i] + "\r\n";
            }
            stdoutStream->WriteNative(out.data(), out.size());
        };

        auto clear = [stdoutStream, options]() {
            std::string clr = "\x1B[" + std::to_string(options.size() + 1) + "A\x1B[J";
            stdoutStream->WriteNative(clr.data(), clr.size());
        };

        render();

        std::shared_ptr<std::function<void()>> readKey = std::make_shared<std::function<void()>>();
        *readKey = [stdinStream, stdoutStream, options, selectedIdx, coro, render, clear, readKey]() mutable {
            lodestdio::StdioStream::PendingRead req;
            req.isCallback = true;
            req.requestedBytes = 1;
            req.callback = Lode::Value();

            auto prevOnData = stdinStream->cppOnData;
            stdinStream->cppOnData = [stdinStream, stdoutStream, options, selectedIdx, coro, render, clear, readKey, prevOnData](const char* data, size_t len) mutable {
                stdinStream->cppOnData = prevOnData;
                if (len == 0) return;
                char ch = data[0];
                if (ch == 3) // Ctrl+C: cancel and wake the yielding caller
                {
                    clear();
                    stdoutStream->WriteNative("\x1B[?25h", 6);
                    uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(&stdinStream->ttyHandle), UV_TTY_MODE_NORMAL);
                    // Resume the yielded thread with an error so it is never
                    // left suspended forever.
                    coro.ResumeError("stdio.select: cancelled by user");
                    return;
                }
                if (ch == '\r' || ch == '\n')
                {
                    stdoutStream->WriteNative("\x1B[?25h", 6);
                    uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(&stdinStream->ttyHandle), UV_TTY_MODE_NORMAL);
                    std::vector<Lode::Value> res;
                    res.push_back(Lode::Value(static_cast<double>(*selectedIdx + 1)));
                    res.push_back(Lode::Value(options[*selectedIdx]));
                    coro.Resume(res);
                    return;
                }
                if (ch == '\x1B' && len >= 3 && data[1] == '[')
                {
                    if (data[2] == 'A' && *selectedIdx > 0)
                    {
                        (*selectedIdx)--;
                        clear();
                        render();
                    }
                    else if (data[2] == 'B' && *selectedIdx + 1 < options.size())
                    {
                        (*selectedIdx)++;
                        clear();
                        render();
                    }
                }
                (*readKey)();
            };
            stdinStream->StartReading();
        };

        (*readKey)();
        return Lode::Value();
    }));

    return Lode::ModuleReturn(exports);
}
