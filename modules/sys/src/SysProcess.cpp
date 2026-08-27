// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sys/SysProcess.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include <uv.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace lodesys
{

struct WriteReq
{
    uv_write_t req;
    std::vector<char> data;
    ProcessPipeStream* stream = nullptr;
};

// =======================================================
// ProcessPipeStream Implementation
// =======================================================

void ProcessPipeStream::InitSignals(Lode::State& vm)
{
    dataSig = Lode::Signal::Create(vm);
    endSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    dataProxy = dataSig->CreatePublic();
    endProxy = endSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void ProcessPipeStream::FireError(const std::string& message)
{
    if (!mgr || mgr->shuttingDown || closed || closing)
        return;
    errorSig->Fire(Lode::Value(message));
}

void ProcessPipeStream::StartReading()
{
    if (reading || closed || closing || !isReadable || !open)
        return;
    reading = true;
    int r = uv_read_start(reinterpret_cast<uv_stream_t*>(&pipe), AllocBuffer, OnRead);
    if (r != 0)
    {
        reading = false;
        FireError(std::string("pipe read: ") + uv_strerror(r));
    }
}

void ProcessPipeStream::StopReading()
{
    if (!reading)
        return;
    reading = false;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&pipe));
}

void ProcessPipeStream::WriteNative(const char* data, size_t size)
{
    if (closed || closing || !open || size == 0 || !data)
        return;

    auto* wr = new WriteReq();
    std::memset(&wr->req, 0, sizeof(wr->req));
    wr->req.data = wr;
    wr->stream = this;
    wr->data.assign(data, data + size);

    uv_buf_t buf;
    buf.base = wr->data.data();
    buf.len = wr->data.size();

    int r = uv_write(&wr->req, reinterpret_cast<uv_stream_t*>(&pipe), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wr;
        FireError(std::string("pipe write: ") + uv_strerror(r));
    }
}

bool ProcessPipeStream::TryResolve(Lode::State& vm, const PendingRead& req, Lode::Value& outVal)
{
    if (req.isLine)
    {
        auto it = std::find(readBuffer.begin(), readBuffer.end(), '\n');
        if (it != readBuffer.end())
        {
            size_t idx = std::distance(readBuffer.begin(), it);
            size_t lineLen = idx;
            if (lineLen > 0 && readBuffer[lineLen - 1] == '\r')
                lineLen--;
            std::string line(reinterpret_cast<char*>(readBuffer.data()), lineLen);
            readBuffer.erase(readBuffer.begin(), it + 1);
            outVal = Lode::Value(line);
            return true;
        }
        return false;
    }

    if (req.requestedBytes == 0)
    {
        if (readBuffer.empty())
            return false;
        if (req.isBuffer)
        {
            Lode::Value b = vm.CreateBuffer(readBuffer.size());
            size_t sz = 0;
            void* ptr = b.AsBuffer(&sz);
            if (ptr) std::memcpy(ptr, readBuffer.data(), readBuffer.size());
            readBuffer.clear();
            outVal = b;
        }
        else
        {
            std::string str(reinterpret_cast<char*>(readBuffer.data()), readBuffer.size());
            readBuffer.clear();
            outVal = Lode::Value(str);
        }
        return true;
    }

    if (readBuffer.size() >= req.requestedBytes)
    {
        if (req.isBuffer)
        {
            Lode::Value b = vm.CreateBuffer(req.requestedBytes);
            size_t sz = 0;
            void* ptr = b.AsBuffer(&sz);
            if (ptr) std::memcpy(ptr, readBuffer.data(), req.requestedBytes);
            readBuffer.erase(readBuffer.begin(), readBuffer.begin() + req.requestedBytes);
            outVal = b;
        }
        else
        {
            std::string str(reinterpret_cast<char*>(readBuffer.data()), req.requestedBytes);
            readBuffer.erase(readBuffer.begin(), readBuffer.begin() + req.requestedBytes);
            outVal = Lode::Value(str);
        }
        return true;
    }

    return false;
}

void ProcessPipeStream::ProcessQueue(bool isEof)
{
    if (!mainL || closed)
        return;

    Lode::State vm(mainL);

    while (!pendingQueue.empty())
    {
        auto req = pendingQueue.front();
        Lode::Value result;
        bool resolved = TryResolve(vm, req, result);

        if (!resolved && isEof)
        {
            if (req.isLine)
            {
                if (!readBuffer.empty())
                {
                    size_t len = readBuffer.size();
                    if (len > 0 && readBuffer[len - 1] == '\r') len--;
                    result = Lode::Value(std::string(reinterpret_cast<char*>(readBuffer.data()), len));
                    readBuffer.clear();
                }
                else
                {
                    result = Lode::Value(); // nil
                }
                resolved = true;
            }
            else
            {
                if (!readBuffer.empty())
                {
                    if (req.isBuffer)
                    {
                        Lode::Value b = vm.CreateBuffer(readBuffer.size());
                        size_t sz = 0;
                        void* ptr = b.AsBuffer(&sz);
                        if (ptr) std::memcpy(ptr, readBuffer.data(), readBuffer.size());
                        result = b;
                    }
                    else
                    {
                        result = Lode::Value(std::string(reinterpret_cast<char*>(readBuffer.data()), readBuffer.size()));
                    }
                    readBuffer.clear();
                }
                else
                {
                    result = req.isBuffer ? vm.CreateBuffer(0) : Lode::Value(std::string(""));
                }
                resolved = true;
            }
        }

        if (resolved)
        {
            pendingQueue.pop_front();
            if (req.coroutine.IsValid())
            {
                std::vector<Lode::Value> resVals;
                if (!result.IsNil())
                    resVals.push_back(result);
                req.coroutine.Resume(resVals);
            }
        }
        else
        {
            break;
        }
    }
}

Lode::Value ProcessPipeStream::MethodWrite(Lode::State& vm, Lode::StackArgs args)
{
    if (isReadable || !open || closed || closing)
    {
        vm.RaiseError("sys.Process stream Write: stream is closed or not writable");
        return Lode::Value();
    }
    if (args.empty())
        return Lode::Value();

    size_t idx = (args.Size() > 1 && !args[0].IsString() && !args[0].IsBuffer()) ? 1 : 0;
    const auto& arg = args[idx];
    if (arg.IsString())
    {
        std::string s = arg.AsString();
        WriteNative(s.data(), s.size());
    }
    else if (arg.IsBuffer())
    {
        size_t sz = 0;
        void* ptr = arg.AsBuffer(&sz);
        if (ptr)
            WriteNative(static_cast<const char*>(ptr), sz);
    }
    else
    {
        vm.RaiseError("sys.Process stream Write: expected string or buffer");
    }
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodWriteLine(Lode::State& vm, Lode::StackArgs args)
{
    if (isReadable || !open || closed || closing)
    {
        vm.RaiseError("sys.Process stream WriteLine: stream is closed or not writable");
        return Lode::Value();
    }
    if (args.empty())
        return Lode::Value();

    size_t idx = (args.Size() > 1 && !args[0].IsString() && !args[0].IsBuffer()) ? 1 : 0;
    const auto& arg = args[idx];
    if (arg.IsString())
    {
        std::string s = arg.AsString() + "\n";
        WriteNative(s.data(), s.size());
    }
    else if (arg.IsBuffer())
    {
        size_t sz = 0;
        void* ptr = arg.AsBuffer(&sz);
        if (ptr)
        {
            std::string s(static_cast<const char*>(ptr), sz);
            s += "\n";
            WriteNative(s.data(), s.size());
        }
    }
    else
    {
        vm.RaiseError("sys.Process stream WriteLine: expected string or buffer");
    }
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodRead(Lode::State& vm, Lode::StackArgs args)
{
    if (!isReadable || !open || closed)
    {
        vm.RaiseError("sys.Process stream Read: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.coroutine = Lode::Coroutine(vm.GetLuaState());

    size_t numIdx = (args.Size() > 1 && !args[0].IsNumber()) ? 1 : 0;
    if (args.Size() > numIdx && args[numIdx].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[numIdx].AsNumber(), "read length");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        req.requestedBytes = res.GetValue();
    }

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        return result;
    }

    pendingQueue.push_back(req);
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodReadBuffer(Lode::State& vm, Lode::StackArgs args)
{
    if (!isReadable || !open || closed)
    {
        vm.RaiseError("sys.Process stream ReadBuffer: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.coroutine = Lode::Coroutine(vm.GetLuaState());
    req.isBuffer = true;

    size_t numIdx = (args.Size() > 1 && !args[0].IsNumber()) ? 1 : 0;
    if (args.Size() > numIdx && args[numIdx].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[numIdx].AsNumber(), "read length");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        req.requestedBytes = res.GetValue();
    }

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        return result;
    }

    pendingQueue.push_back(req);
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodReadLine(Lode::State& vm, Lode::StackArgs)
{
    if (!isReadable || !open || closed)
    {
        vm.RaiseError("sys.Process stream ReadLine: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.coroutine = Lode::Coroutine(vm.GetLuaState());
    req.isLine = true;

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        return result;
    }

    pendingQueue.push_back(req);
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodStartStreaming(Lode::State& vm)
{
    if (!isReadable || !open || closed)
    {
        vm.RaiseError("sys.Process stream StartStreaming: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();
    StartReading();
    return Lode::Value();
}

Lode::Value ProcessPipeStream::MethodStopStreaming(Lode::State&)
{
    StopReading();
    return Lode::Value();
}

void ProcessPipeStream::RequestClose()
{
    if (closed || closing)
        return;
    closing = true;
    StopReading();
    pendingQueue.clear();

    if (pipeInited)
    {
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void ProcessPipeStream::FinishClosed()
{
    closing = false;
    closed = true;
    open = false;
    if (mgr)
        mgr->RemoveStream(shared_from_this());
    selfGuard.reset();
}

void ProcessPipeStream::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<ProcessPipeStream*>(handle->data);
    if (self)
        self->FinishClosed();
}

void ProcessPipeStream::AllocBuffer(uv_handle_t*, size_t suggestedSize, uv_buf_t* buf)
{
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void ProcessPipeStream::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<ProcessPipeStream*>(stream->data);
    if (!self || self->closing || self->closed)
    {
        if (buf->base) delete[] buf->base;
        return;
    }

    if (nread > 0)
    {
        self->readBuffer.insert(self->readBuffer.end(), buf->base, buf->base + nread);

        if (self->mainL)
        {
            Lode::State vm(self->mainL);
            Lode::Value b = vm.CreateBuffer(static_cast<size_t>(nread));
            size_t sz = 0;
            void* ptr = b.AsBuffer(&sz);
            if (ptr) std::memcpy(ptr, buf->base, static_cast<size_t>(nread));
            self->dataSig->Fire(b);
        }

        self->ProcessQueue(false);
    }
    else if (nread < 0)
    {
        if (nread == UV_EOF)
        {
            self->endSig->Fire();
            self->ProcessQueue(true);
        }
        else
        {
            self->FireError(std::string("read: ") + uv_strerror(static_cast<int>(nread)));
        }
        self->StopReading();
    }

    if (buf->base)
        delete[] buf->base;
}

void ProcessPipeStream::OnWritten(uv_write_t* req, int status)
{
    auto* wr = static_cast<WriteReq*>(req->data);
    if (status != 0 && wr && wr->stream)
    {
        wr->stream->FireError(std::string("write: ") + uv_strerror(status));
    }
    delete wr;
}

Lode::Value WrapProcessStream(Lode::State& vm, const std::shared_ptr<ProcessPipeStream>& s, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunction([s, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        const std::string_view key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsStringView() : std::string_view();
        if (key == "DataReceived") return s->dataProxy;
        if (key == "EndOfStream") return s->endProxy;
        if (key == "ErrorOccurred") return s->errorProxy;

        auto val = methods.Get(std::string(key));
        if (val.IsOk() && !val.GetValue().IsNil())
            return val.GetValue();

        return Lode::Value();
    }));

    meta.Set("__newindex", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("ProcessStream: objects are read-only");
        return Lode::Value();
    }));

    meta.Set("__metatable", Lode::Value(std::string("ProcessStream")));
    meta.Set("__tostring", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(std::string("ProcessStream"));
    }));

    Lode::ObjectWrap<ProcessPipeStream>::Wrap(vm, s, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

// =======================================================
// Process Implementation
// =======================================================

void Process::InitSignals(Lode::State& vm)
{
    exitSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    exitProxy = exitSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void Process::FireError(const std::string& message)
{
    if (!mgr || mgr->shuttingDown)
        return;
    errorSig->Fire(Lode::Value(message));
}

void Process::OnExit(int64_t exit_status, int term_signal)
{
    running = false;
    exited = true;
    exitCode = exit_status;
    termSignal = term_signal;

    if (mainL)
    {
        exitSig->Fire({ Lode::Value(static_cast<double>(exit_status)), Lode::Value(static_cast<double>(term_signal)) });
    }

    for (auto& co : waitCoroutines)
    {
        if (co.IsValid())
        {
            std::vector<Lode::Value> resVals;
            resVals.push_back(Lode::Value(static_cast<double>(exit_status)));
            resVals.push_back(Lode::Value(static_cast<double>(term_signal)));
            co.Resume(resVals);
        }
    }
    waitCoroutines.clear();

    RequestClose();
}

void Process::RequestClose()
{
    if (handleClosed)
        return;
    handleClosed = true;

    if (stdinStream) stdinStream->RequestClose();
    if (stdoutStream) stdoutStream->RequestClose();
    if (stderrStream) stderrStream->RequestClose();

    uv_close(reinterpret_cast<uv_handle_t*>(&process), OnProcessClosed);
}

void Process::OnProcessExit(uv_process_t* req, int64_t exit_status, int term_signal)
{
    auto* self = static_cast<Process*>(req->data);
    if (self)
        self->OnExit(exit_status, term_signal);
}

void Process::OnProcessClosed(uv_handle_t* handle)
{
    auto* self = static_cast<Process*>(handle->data);
    if (self)
    {
        if (self->mgr)
            self->mgr->RemoveProcess(self->shared_from_this());
        self->selfGuard.reset();
    }
}

Lode::Value Process::MethodKill(Lode::State& vm, Lode::StackArgs args)
{
    if (!running)
        return Lode::Value();

    int signum = 15; // SIGTERM default
    size_t idx = (args.Size() > 1 && !args[0].IsNumber()) ? 1 : 0;
    if (args.Size() > idx && args[idx].IsNumber())
    {
        signum = static_cast<int>(args[idx].AsNumber());
    }

    int r = uv_process_kill(&process, signum);
    if (r != 0)
    {
        FireError(std::string("kill: ") + uv_strerror(r));
    }
    return Lode::Value();
}

Lode::Value Process::MethodWait(Lode::State& vm)
{
    if (exited)
    {
        // Deliver through a deferred resume so the arity always matches the
        // yielding path: (exitCode, termSignal).
        mainL = vm.GetMainThread();
        Lode::Coroutine coro(vm.GetLuaState());
        waitCoroutines.push_back(coro);
        std::vector<Lode::Value> resVals;
        resVals.push_back(Lode::Value(static_cast<double>(exitCode)));
        resVals.push_back(Lode::Value(static_cast<double>(termSignal)));
        Lode::Task::Defer(vm, Lode::Value(coro), resVals);
        vm.YieldThread();
        return Lode::Value();
    }

    mainL = vm.GetMainThread();
    Lode::Coroutine coro(vm.GetLuaState());
    waitCoroutines.push_back(coro);
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value Process::MethodGetPid(Lode::State&)
{
    return Lode::Value(static_cast<double>(pid));
}

Lode::Value Process::MethodGetExitCode(Lode::State&)
{
    if (!exited) return Lode::Value();
    return Lode::Value(static_cast<double>(exitCode));
}

Lode::Value Process::MethodGetTermSignal(Lode::State&)
{
    if (!exited) return Lode::Value();
    return Lode::Value(static_cast<double>(termSignal));
}

Lode::Value Process::MethodIsRunning(Lode::State&)
{
    return Lode::Value(running);
}

Lode::Value Process::MethodClose(Lode::State&)
{
    RequestClose();
    return Lode::Value();
}

Lode::Value WrapProcess(Lode::State& vm, const std::shared_ptr<Process>& p, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunction([p, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        const std::string_view key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsStringView() : std::string_view();
        if (key == "pid") return Lode::Value(static_cast<double>(p->pid));
        if (key == "Exited") return p->exitProxy;
        if (key == "ErrorOccurred") return p->errorProxy;

        if (key == "stdin")
        {
            if (p->stdinStream)
                return WrapProcessStream(vm2, p->stdinStream, p->mgr->streamMethods);
            return Lode::Value();
        }
        if (key == "stdout")
        {
            if (p->stdoutStream)
                return WrapProcessStream(vm2, p->stdoutStream, p->mgr->streamMethods);
            return Lode::Value();
        }
        if (key == "stderr")
        {
            if (p->stderrStream)
                return WrapProcessStream(vm2, p->stderrStream, p->mgr->streamMethods);
            return Lode::Value();
        }

        auto val = methods.Get(std::string(key));
        if (val.IsOk() && !val.GetValue().IsNil())
            return val.GetValue();

        return Lode::Value();
    }));

    meta.Set("__newindex", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("Process: objects are read-only");
        return Lode::Value();
    }));

    meta.Set("__metatable", Lode::Value(std::string("Process")));
    meta.Set("__tostring", vm.CreateFastFunction([p](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value("Process(pid=" + std::to_string(p->pid) + ")");
    }));

    Lode::ObjectWrap<Process>::Wrap(vm, p, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

// =======================================================
// ProcessManager Implementation
// =======================================================

void ProcessManager::AddProcess(const std::shared_ptr<Process>& p)
{
    processes.push_back(p);
}

void ProcessManager::RemoveProcess(const std::shared_ptr<Process>& p)
{
    auto it = std::find(processes.begin(), processes.end(), p);
    if (it != processes.end())
        processes.erase(it);
}

void ProcessManager::AddStream(const std::shared_ptr<ProcessPipeStream>& s)
{
    streams.push_back(s);
}

void ProcessManager::RemoveStream(const std::shared_ptr<ProcessPipeStream>& s)
{
    auto it = std::find(streams.begin(), streams.end(), s);
    if (it != streams.end())
        streams.erase(it);
}

void ProcessManager::Shutdown()
{
    shuttingDown = true;
    auto pCopy = processes;
    for (auto& p : pCopy)
    {
        if (p)
        {
            if (p->running)
            {
                Lode::State vm(mainL);
                p->MethodKill(vm, Lode::StackArgs(mainL));
            }
            p->RequestClose();
        }
    }
    processes.clear();

    auto sCopy = streams;
    for (auto& s : sCopy)
    {
        if (s)
            s->RequestClose();
    }
    streams.clear();
}

static Lode::Table BuildProcessStreamMethods(Lode::State& vm, const std::shared_ptr<ProcessManager>&)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Write", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream Write: invalid stream"); return Lode::Value(); }
        return self->MethodWrite(vm2, args);
    }));

    m.Set("WriteLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream WriteLine: invalid stream"); return Lode::Value(); }
        return self->MethodWriteLine(vm2, args);
    }));

    m.Set("Read", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream Read: invalid stream"); return Lode::Value(); }
        return self->MethodRead(vm2, args);
    }));

    m.Set("ReadBuffer", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream ReadBuffer: invalid stream"); return Lode::Value(); }
        return self->MethodReadBuffer(vm2, args);
    }));

    m.Set("ReadLine", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream ReadLine: invalid stream"); return Lode::Value(); }
        return self->MethodReadLine(vm2, args);
    }));

    m.Set("StartStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream StartStreaming: invalid stream"); return Lode::Value(); }
        return self->MethodStartStreaming(vm2);
    }));

    m.Set("StopStreaming", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream StopStreaming: invalid stream"); return Lode::Value(); }
        return self->MethodStopStreaming(vm2);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<ProcessPipeStream>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ProcessStream Close: invalid stream"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    return m;
}

static Lode::Table BuildProcessMethods(Lode::State& vm, const std::shared_ptr<ProcessManager>&)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Kill", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Process Kill: invalid process"); return Lode::Value(); }
        return self->MethodKill(vm2, args);
    }));

    m.Set("Wait", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Process Wait: invalid process"); return Lode::Value(); }
        return self->MethodWait(vm2);
    }));

    m.Set("GetPid", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(0.0);
        return self->MethodGetPid(vm2);
    }));

    m.Set("GetExitCode", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) return Lode::Value();
        return self->MethodGetExitCode(vm2);
    }));

    m.Set("GetTermSignal", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) return Lode::Value();
        return self->MethodGetTermSignal(vm2);
    }));

    m.Set("IsRunning", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(false);
        return self->MethodIsRunning(vm2);
    }));

    m.Set("Close", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        auto self = Lode::ObjectWrap<Process>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Process Close: invalid process"); return Lode::Value(); }
        return self->MethodClose(vm2);
    }));

    m.Set("Destroy", m.Get("Close").GetValue());

    return m;
}

// =======================================================
// Spawn Function
// =======================================================

static Lode::Value SpawnProcess(Lode::State& vm, const std::shared_ptr<ProcessManager>& mgr, Lode::StackArgs args)
{
    if (mgr->shuttingDown)
    {
        vm.RaiseError("sys.spawn: runtime is shutting down");
        return Lode::Value();
    }

    std::string command;
    Lode::Table optionsTbl;
    bool hasOpts = false;

    if (args.empty())
    {
        vm.RaiseError("sys.spawn: expected command string or options table");
        return Lode::Value();
    }

    if (args[0].IsString())
    {
        command = args[0].AsString();
        if (args.Size() > 1 && args[1].IsTable())
        {
            optionsTbl = args[1].AsTable();
            hasOpts = true;
        }
    }
    else if (args[0].IsTable())
    {
        optionsTbl = args[0].AsTable();
        hasOpts = true;
        auto cmdVal = optionsTbl.Get("command");
        if (cmdVal.IsOk() && cmdVal.GetValue().IsString())
            command = cmdVal.GetValue().AsString();
        else
        {
            auto fileVal = optionsTbl.Get("file");
            if (fileVal.IsOk() && fileVal.GetValue().IsString())
                command = fileVal.GetValue().AsString();
            else
            {
                auto firstVal = optionsTbl.Get(1);
                if (firstVal.IsOk() && firstVal.GetValue().IsString())
                    command = firstVal.GetValue().AsString();
            }
        }
    }
    else
    {
        vm.RaiseError("sys.spawn: expected string or table for argument 1");
        return Lode::Value();
    }

    if (command.empty())
    {
        vm.RaiseError("sys.spawn: command cannot be empty");
        return Lode::Value();
    }

    std::vector<std::string> argStrings;
    argStrings.push_back(command);

    if (hasOpts)
    {
        auto argsVal = optionsTbl.Get("args");
        if (argsVal.IsOk() && argsVal.GetValue().IsTable())
        {
            Lode::Table aTbl = argsVal.GetValue().AsTable();
            int i = 1;
            while (true)
            {
                auto itm = aTbl.Get(i);
                if (itm.IsError() || !itm.GetValue().IsString())
                    break;
                argStrings.push_back(itm.GetValue().AsString());
                i++;
            }
        }
    }

    std::vector<char*> argPointers;
    for (auto& s : argStrings)
        argPointers.push_back(s.data());
    argPointers.push_back(nullptr);

    std::string cwdStr;
    const char* cwdPtr = nullptr;
    if (hasOpts)
    {
        auto cwdVal = optionsTbl.Get("cwd");
        if (cwdVal.IsOk() && cwdVal.GetValue().IsString())
        {
            cwdStr = cwdVal.GetValue().AsString();
            cwdPtr = cwdStr.c_str();
        }
    }

    std::vector<std::string> envStrings;
    std::vector<char*> envPointers;
    char** envPtr = nullptr;
    if (hasOpts)
    {
        auto envVal = optionsTbl.Get("env");
        if (envVal.IsOk() && envVal.GetValue().IsTable())
        {
            Lode::Table eTbl = envVal.GetValue().AsTable();
            auto keys = eTbl.GetKeys();
            for (const auto& k : keys)
            {
                auto v = eTbl.Get(k);
                if (v.IsOk() && v.GetValue().IsString())
                {
                    envStrings.push_back(k + "=" + v.GetValue().AsString());
                }
            }
            for (auto& es : envStrings)
                envPointers.push_back(es.data());
            envPointers.push_back(nullptr);
            envPtr = envPointers.data();
        }
    }

    std::string stdioCfg[3] = { "pipe", "pipe", "pipe" };
    if (hasOpts)
    {
        auto stdioVal = optionsTbl.Get("stdio");
        if (stdioVal.IsOk())
        {
            if (stdioVal.GetValue().IsString())
            {
                std::string mode = stdioVal.GetValue().AsString();
                stdioCfg[0] = stdioCfg[1] = stdioCfg[2] = mode;
            }
            else if (stdioVal.GetValue().IsTable())
            {
                Lode::Table sTbl = stdioVal.GetValue().AsTable();
                for (int i = 0; i < 3; ++i)
                {
                    auto v = sTbl.Get(i + 1);
                    if (v.IsOk() && v.GetValue().IsString())
                        stdioCfg[i] = v.GetValue().AsString();
                }
            }
        }
    }

    auto proc = std::make_shared<Process>();
    proc->mgr = mgr;
    proc->mainL = mgr->mainL;
    proc->loop = mgr->loop;
    proc->process.data = proc.get();

    uv_stdio_container_t childStdio[3];
    std::memset(childStdio, 0, sizeof(childStdio));

    // Stdin (index 0)
    if (stdioCfg[0] == "pipe")
    {
        auto s = std::make_shared<ProcessPipeStream>();
        s->mgr = mgr;
        s->mainL = mgr->mainL;
        s->loop = mgr->loop;
        s->isReadable = false;
        s->open = true;
        s->pipeInited = true;
        s->pipe.data = s.get();
        uv_pipe_init(mgr->loop, &s->pipe, 0);
        s->InitSignals(vm);
        mgr->AddStream(s);
        s->selfGuard = s;
        proc->stdinStream = s;

        childStdio[0].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
        childStdio[0].data.stream = reinterpret_cast<uv_stream_t*>(&s->pipe);
    }
    else if (stdioCfg[0] == "inherit")
    {
        childStdio[0].flags = UV_INHERIT_FD;
        childStdio[0].data.fd = 0;
    }
    else
    {
        childStdio[0].flags = UV_IGNORE;
    }

    // Stdout (index 1)
    if (stdioCfg[1] == "pipe")
    {
        auto s = std::make_shared<ProcessPipeStream>();
        s->mgr = mgr;
        s->mainL = mgr->mainL;
        s->loop = mgr->loop;
        s->isReadable = true;
        s->open = true;
        s->pipeInited = true;
        s->pipe.data = s.get();
        uv_pipe_init(mgr->loop, &s->pipe, 0);
        s->InitSignals(vm);
        mgr->AddStream(s);
        s->selfGuard = s;
        proc->stdoutStream = s;

        childStdio[1].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        childStdio[1].data.stream = reinterpret_cast<uv_stream_t*>(&s->pipe);
    }
    else if (stdioCfg[1] == "inherit")
    {
        childStdio[1].flags = UV_INHERIT_FD;
        childStdio[1].data.fd = 1;
    }
    else
    {
        childStdio[1].flags = UV_IGNORE;
    }

    // Stderr (index 2)
    if (stdioCfg[2] == "pipe")
    {
        auto s = std::make_shared<ProcessPipeStream>();
        s->mgr = mgr;
        s->mainL = mgr->mainL;
        s->loop = mgr->loop;
        s->isReadable = true;
        s->open = true;
        s->pipeInited = true;
        s->pipe.data = s.get();
        uv_pipe_init(mgr->loop, &s->pipe, 0);
        s->InitSignals(vm);
        mgr->AddStream(s);
        s->selfGuard = s;
        proc->stderrStream = s;

        childStdio[2].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        childStdio[2].data.stream = reinterpret_cast<uv_stream_t*>(&s->pipe);
    }
    else if (stdioCfg[2] == "inherit")
    {
        childStdio[2].flags = UV_INHERIT_FD;
        childStdio[2].data.fd = 2;
    }
    else
    {
        childStdio[2].flags = UV_IGNORE;
    }

    proc->options.exit_cb = Process::OnProcessExit;
    proc->options.file = command.c_str();
    proc->options.args = argPointers.data();
    proc->options.env = envPtr;
    proc->options.cwd = cwdPtr;
    proc->options.flags = 0;
    proc->options.stdio_count = 3;
    proc->options.stdio = childStdio;

    if (hasOpts)
    {
        auto detVal = optionsTbl.Get("detached");
        if (detVal.IsOk() && detVal.GetValue().AsBoolean())
            proc->options.flags |= UV_PROCESS_DETACHED;
#ifdef _WIN32
        auto hideVal = optionsTbl.Get("hide");
        if (hideVal.IsOk() && hideVal.GetValue().AsBoolean())
            proc->options.flags |= UV_PROCESS_WINDOWS_HIDE;
#endif
    }

    int r = uv_spawn(mgr->loop, &proc->process, &proc->options);
    if (r != 0)
    {
        // uv_spawn initializes the process handle before reporting an exec
        // failure. Keep the owner alive until libuv finishes closing it;
        // otherwise the loop retains a dangling uv_process_t.
        proc->selfGuard = proc;
        proc->RequestClose();
        vm.RaiseError(std::string("sys.spawn error: ") + uv_strerror(r));
        return Lode::Value();
    }

    proc->pid = proc->process.pid;
    proc->running = true;
    proc->InitSignals(vm);
    mgr->AddProcess(proc);
    proc->selfGuard = proc;

    return WrapProcess(vm, proc, mgr->processMethods);
}

// =======================================================
// Native C++ APIs
// =======================================================

std::string GuessHandleType(int fd)
{
    uv_handle_type t = uv_guess_handle(static_cast<uv_file>(fd));
    switch (t) {
        case UV_TTY: return "tty";
        case UV_NAMED_PIPE: return "pipe";
        case UV_FILE: return "file";
        case UV_TCP: return "tcp";
        case UV_UDP: return "udp";
        default: return "unknown";
    }
}

void BindSysProcess(Lode::State& vm, Lode::Table& exports, const std::shared_ptr<ProcessManager>& mgr)
{
    mgr->processMethods = BuildProcessMethods(vm, mgr);
    mgr->streamMethods = BuildProcessStreamMethods(vm, mgr);

    exports.Set("GetEnv", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString()) {
            vm.RaiseError("sys.GetEnv: expected string argument");
            return Lode::Value();
        }
        std::string key = args[0].AsString();
        // Grow-and-retry so values larger than the initial buffer are returned
        // instead of raising ENOBUFS (same pattern as GetTmpDir/GetHomeDir).
        size_t cap = 4096;
        std::string value;
        for (;;) {
            std::vector<char> buf(cap);
            size_t size = buf.size();
            int r = uv_os_getenv(key.c_str(), buf.data(), &size);
            if (r == UV_ENOENT) {
                return Lode::Value();
            }
            if (r == UV_ENOBUFS) {
                if (size > (1u << 20)) { // sane upper bound (1 MiB)
                    vm.RaiseError("sys.GetEnv error: value too large");
                    return Lode::Value();
                }
                cap = size + 1;
                continue;
            }
            if (r < 0) {
                vm.RaiseError(std::string("sys.GetEnv error: ") + uv_strerror(r));
                return Lode::Value();
            }
            value.assign(buf.data(), size);
            return Lode::Value(value);
        }
    }));

    exports.Set("SetEnv", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsString() || !args[1].IsString()) {
            vm.RaiseError("sys.SetEnv: expected (string, string) arguments");
            return Lode::Value();
        }
        std::string key = args[0].AsString();
        std::string val = args[1].AsString();
        int r = uv_os_setenv(key.c_str(), val.c_str());
        if (r < 0) {
            vm.RaiseError(std::string("sys.SetEnv error: ") + uv_strerror(r));
        }
        return Lode::Value();
    }));

    exports.Set("GetCwd", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
        char buf[4096];
        size_t size = sizeof(buf);
        int r = uv_cwd(buf, &size);
        if (r < 0) {
            vm.RaiseError(std::string("sys.GetCwd error: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value(std::string(buf, size));
    }));

    exports.Set("Chdir", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString()) {
            vm.RaiseError("sys.Chdir: expected string argument");
            return Lode::Value();
        }
        std::string dir = args[0].AsString();
        int r = uv_chdir(dir.c_str());
        if (r < 0) {
            vm.RaiseError(std::string("sys.Chdir error: ") + uv_strerror(r));
        }
        return Lode::Value();
    }));

    exports.Set("exit", vm.CreateFastFunction([mgr](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        int code = 0;
        if (args.Size() > 0 && args[0].IsNumber()) {
            code = static_cast<int>(args[0].AsNumber());
        }
        // Drain pending async writes (stdio etc.) before terminating so
        // queued uv_write requests are flushed instead of being lost.
        // Hard-capped at 500 ms so live timers cannot hang the process.
        uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
        const uint64_t deadlineNs = uv_hrtime() + static_cast<uint64_t>(500) * 1000 * 1000;
        while (uv_loop_alive(loop) != 0) {
            if (uv_run(loop, UV_RUN_ONCE) <= 0)
                break;
            if (uv_hrtime() > deadlineNs)
                break;
        }
        std::_Exit(code);
    }));

    exports.Set("GuessHandleType", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsNumber()) {
            vm.RaiseError("sys.GuessHandleType: expected number argument");
            return Lode::Value();
        }
        int fd = static_cast<int>(args[0].AsNumber());
        return Lode::Value(GuessHandleType(fd));
    }));

    exports.Set("spawn", vm.CreateFastFunction([mgr](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        return SpawnProcess(vm2, mgr, args);
    }));

    exports.Set("Spawn", exports.Get("spawn").GetValue());

    exports.Set("Args", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
        auto args = vm.GetCliArgs();
        Lode::Table t = vm.CreateTable();
        for (size_t i = 0; i < args.size(); ++i)
            t.Set(static_cast<int>(i + 1), Lode::Value(args[i]));
        return Lode::Value(t);
    }));
    exports.Set("GetArgs", exports.Get("Args").GetValue());
}

} // namespace lodesys
