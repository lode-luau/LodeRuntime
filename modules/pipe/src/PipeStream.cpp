// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Pipe/PipeStream.hpp"
#include "Pipe/PipeHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include <cstring>
#include <algorithm>

namespace lodepipe
{

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

void PipeStream::InitSignals(Lode::State& vm)
{
    dataSig = Lode::Signal::Create(vm);
    endSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    dataProxy = dataSig->CreatePublic();
    endProxy = endSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void PipeStream::FireError(const std::string& message)
{
    if (mgr->shuttingDown || closed || closing)
        return;
    if (cppOnError)
    {
        cppOnError(message);
        return;
    }
    errorSig->Fire(Lode::Value(message));
}

void PipeStream::StartReading()
{
    if (reading || closed || closing)
        return;
    reading = true;
    int r = uv_read_start(reinterpret_cast<uv_stream_t*>(&pipe), AllocBuffer, OnRead);
    if (r != 0)
    {
        reading = false;
        FireError(std::string("read: ") + uv_strerror(r));
        RequestClose();
    }
}

void PipeStream::StopReading()
{
    if (!reading)
        return;
    reading = false;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&pipe));
}

std::string PipeStream::OpenFdNative(int fd)
{
    if (open || closing || closed)
        return "stream is already open or closed";
    std::memset(&pipe, 0, sizeof(pipe));
    pipe.data = this;
    int r = uv_pipe_init(loop, &pipe, 0);
    if (r != 0)
        return std::string("pipe init: ") + uv_strerror(r);
    pipeInited = true;
    r = uv_pipe_open(&pipe, fd);
    if (r != 0)
    {
        RequestClose();
        return std::string("pipe open: ") + uv_strerror(r);
    }
    open = true;
    return "";
}

void PipeStream::ConnectNative(const std::string& path)
{
    if (open || connectPending || closing || closed)
        return;
    std::memset(&pipe, 0, sizeof(pipe));
    pipe.data = this;
    int r = uv_pipe_init(loop, &pipe, 0);
    if (r != 0)
    {
        FailConnect(std::string("pipe init: ") + uv_strerror(r));
        return;
    }
    pipeInited = true;
    std::memset(&connReq, 0, sizeof(connReq));
    connReq.data = this;
    uv_pipe_connect(&connReq, &pipe, path.c_str(), OnConnected);
}

void PipeStream::WriteNative(const char* data, size_t size)
{
    if (closed || closing || !open)
        return;
    std::vector<char> vec;
    if (size > 0 && data)
        vec.assign(data, data + size);
    auto* wreq = new WriteRequest();
    std::memset(&wreq->req, 0, sizeof(wreq->req));
    wreq->req.data = this;
    wreq->data = std::move(vec);
    uv_buf_t buf;
    buf.base = wreq->data.empty() ? const_cast<char*>("") : wreq->data.data();
    buf.len = wreq->data.size();
    int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&pipe), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("write: ") + uv_strerror(r));
    }
}

Lode::Value PipeStream::MethodConnect(Lode::State& vm, Lode::StackArgs args)
{
    if (open)
    {
        vm.RaiseError("pipe Connect: already open");
        return Lode::Value();
    }
    if (connectPending)
    {
        vm.RaiseError("pipe Connect: a connection attempt is already in progress");
        return Lode::Value();
    }
    if (closing || closed)
    {
        vm.RaiseError("pipe Connect: stream is closed");
        return Lode::Value();
    }
    if (args.Size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("pipe Connect: path must be a string");
        return Lode::Value();
    }
    std::string raw = args[1].AsString();
    if (raw.empty())
    {
        vm.RaiseError("pipe Connect: path must not be empty");
        return Lode::Value();
    }
    std::string path = NormalizePipePath(raw);

    connectPath = path;
    connectPending = true;
    connectCo = Lode::Coroutine(vm.GetLuaState());

    std::memset(&pipe, 0, sizeof(pipe));
    pipe.data = this;
    int r = uv_pipe_init(loop, &pipe, 0);
    if (r != 0)
    {
        connectPending = false;
        connectCo = Lode::Coroutine();
        vm.RaiseError("pipe Connect: " + std::string(uv_strerror(r)));
        return Lode::Value();
    }
    pipeInited = true;
    std::memset(&connReq, 0, sizeof(connReq));
    connReq.data = this;
    uv_pipe_connect(&connReq, &pipe, path.c_str(), OnConnected);
    return vm.YieldThread();
}

Lode::Value PipeStream::MethodOpenFD(Lode::State& vm, Lode::StackArgs args)
{
    if (open)
    {
        vm.RaiseError("pipe OpenFD: already open");
        return Lode::Value();
    }
    if (closing || closed)
    {
        vm.RaiseError("pipe OpenFD: stream is closed");
        return Lode::Value();
    }
    if (args.Size() < 2 || !args[1].IsNumber())
    {
        vm.RaiseError("pipe OpenFD: fd must be a number");
        return Lode::Value();
    }
    auto fd = Lode::Numeric::ToInt64(args[1].AsNumber(), "fd");
    if (fd.IsError())
    {
        vm.RaiseError(fd.GetError().ErrorMessage());
        return Lode::Value();
    }
    std::string err = OpenFdNative(static_cast<int>(fd.GetValue()));
    if (!err.empty())
    {
        vm.RaiseError("pipe OpenFD: " + err);
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value PipeStream::MethodWrite(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing)
    {
        vm.RaiseError("pipe Write: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("pipe Write: stream is not open");
        return Lode::Value();
    }
    if (args.Size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
    {
        vm.RaiseError("pipe Write: data must be a string or buffer");
        return Lode::Value();
    }
    std::vector<char> data;
    if (args[1].IsString())
    {
        const std::string& text = args[1].AsString();
        data.assign(text.begin(), text.end());
    }
    else
    {
        size_t size = 0;
        void* ptr = args[1].AsBuffer(&size);
        if (ptr && size > 0)
            data.assign(static_cast<const char*>(ptr), static_cast<const char*>(ptr) + size);
    }
    auto* wreq = new WriteRequest();
    std::memset(&wreq->req, 0, sizeof(wreq->req));
    wreq->req.data = this;
    wreq->data = std::move(data);
    uv_buf_t buf;
    buf.base = wreq->data.empty() ? const_cast<char*>("") : wreq->data.data();
    buf.len = wreq->data.size();
    int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&pipe), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("write: ") + uv_strerror(r));
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value PipeStream::MethodWriteLine(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing)
    {
        vm.RaiseError("pipe WriteLine: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("pipe WriteLine: stream is not open");
        return Lode::Value();
    }
    if (args.Size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("pipe WriteLine: data must be a string");
        return Lode::Value();
    }
    std::string text = args[1].AsString();
    text += "\n";
    WriteNative(text.data(), text.size());
    return Lode::Value();
}

Lode::Value PipeStream::MethodRead(Lode::State& vm, Lode::StackArgs args)
{
    if (!open)
    {
        vm.RaiseError("pipe Read: stream is not open");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("pipe Read: already reading");
        return Lode::Value();
    }
    int bytes = -1;
    if (args.Size() > 1 && !args[1].IsNil())
    {
        if (!args[1].IsNumber())
        {
            vm.RaiseError("pipe Read: bytes must be a number or nil");
            return Lode::Value();
        }
        auto n = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
        if (n.IsError())
        {
            vm.RaiseError(n.GetError().ErrorMessage());
            return Lode::Value();
        }
        bytes = static_cast<int>(n.GetValue());
    }
    yieldThread = vm.GetLuaState();
    yieldBytes = bytes;
    yieldingLine = false;
    yieldAsBuffer = false;
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value PipeStream::MethodReadBuffer(Lode::State& vm, Lode::StackArgs args)
{
    if (!open)
    {
        vm.RaiseError("pipe ReadBuffer: stream is not open");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("pipe Read: already reading");
        return Lode::Value();
    }
    int bytes = -1;
    if (args.Size() > 1 && !args[1].IsNil())
    {
        if (!args[1].IsNumber())
        {
            vm.RaiseError("pipe ReadBuffer: bytes must be a number or nil");
            return Lode::Value();
        }
        auto n = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
        if (n.IsError())
        {
            vm.RaiseError(n.GetError().ErrorMessage());
            return Lode::Value();
        }
        bytes = static_cast<int>(n.GetValue());
    }
    yieldThread = vm.GetLuaState();
    yieldBytes = bytes;
    yieldingLine = false;
    yieldAsBuffer = true;
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value PipeStream::MethodReadLine(Lode::State& vm, Lode::StackArgs args)
{
    (void)args;
    if (!open)
    {
        vm.RaiseError("pipe ReadLine: stream is not open");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("pipe Read: already reading");
        return Lode::Value();
    }
    yieldThread = vm.GetLuaState();
    yieldBytes = -1;
    yieldingLine = true;
    yieldAsBuffer = false;
    StartReading();
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value PipeStream::MethodStartStreaming(Lode::State& vm)
{
    if (!open)
    {
        vm.RaiseError("pipe StartStreaming: stream is not open");
        return Lode::Value();
    }
    StartReading();
    return Lode::Value();
}

Lode::Value PipeStream::MethodStopStreaming(Lode::State& vm)
{
    (void)vm;
    StopReading();
    return Lode::Value();
}

void PipeStream::RequestClose()
{
    if (closing)
        return;
    closing = true;
    if (yieldThread)
        FinishYield(Lode::Value());
    if (connectPending && connectCo.IsValid())
    {
        Lode::State vm(mainL);
        auto res = connectCo.ResumeError("connection closed");
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }
    connectPending = false;
    if (reading)
        StopReading();
    if (pipeInited && !pipeClosed)
    {
        pipeClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void PipeStream::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    open = false;
    // Only signal end-of-stream for streams that were actually opened at
    // some point; Create-then-Close must not fire EndOfStream.
    if (endSig && pipeInited)
        endSig->Fire(Lode::Value());
    mgr->RemoveStream(shared_from_this());
    selfGuard.reset();
}

void PipeStream::CheckYield(ssize_t nread, const uv_buf_t* buf)
{
    if (nread < 0)
    {
        if (nread != UV_EOF)
            FireError(std::string("pipe Read error: ") + uv_strerror(nread));
        // At EOF, deliver whatever is still buffered so a pending Read or
        // ReadLine never silently drops data by resuming with nil.
        if (yieldThread && !lineBuffer.empty())
        {
            std::string rest = lineBuffer;
            lineBuffer.clear();
            if (yieldAsBuffer)
            {
                Lode::State vm(mainL);
                Lode::Value bufVal = vm.CreateBuffer(rest.size());
                void* ptr = bufVal.AsBuffer(nullptr);
                if (ptr)
                    std::memcpy(ptr, rest.data(), rest.size());
                FinishYield(bufVal);
            }
            else
            {
                FinishYield(Lode::Value(rest));
            }
            return; // RequestClose follows through the close callback path
        }
        RequestClose();
        return;
    }
    if (nread == 0)
        return;

    if (yieldThread)
    {
        if (yieldingLine)
        {
            lineBuffer.append(buf->base, nread);
            size_t pos = lineBuffer.find('\n');
            if (pos != std::string::npos)
            {
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer = lineBuffer.substr(pos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                FinishYield(Lode::Value(line));
            }
        }
        else
        {
            lineBuffer.append(buf->base, nread);
            if (yieldBytes < 0 || lineBuffer.size() >= static_cast<size_t>(yieldBytes))
            {
                std::string chunk = yieldBytes < 0 ? lineBuffer : lineBuffer.substr(0, yieldBytes);
                lineBuffer = yieldBytes < 0 ? "" : lineBuffer.substr(yieldBytes);
                if (yieldAsBuffer)
                {
                    Lode::State vm(mainL);
                    Lode::Value bufVal = vm.CreateBuffer(chunk.size());
                    void* ptr = bufVal.AsBuffer(nullptr);
                    if (ptr)
                        std::memcpy(ptr, chunk.data(), chunk.size());
                    FinishYield(bufVal);
                }
                else
                {
                    FinishYield(Lode::Value(chunk));
                }
            }
        }
    }
    else if (reading && dataSig)
    {
        std::string s(buf->base, nread);
        dataSig->Fire(Lode::Value(s));
    }
}

void PipeStream::FinishYield(const Lode::Value& val)
{
    if (!yieldThread)
        return;
    Lode::Coroutine co(yieldThread);
    yieldThread = nullptr;
    yieldingLine = false;
    StopReading();
    co.Resume({ val });
}

void PipeStream::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<PipeStream*>(handle->data);
    self->FinishClosed();
}

void PipeStream::OnConnected(uv_connect_t* req, int status)
{
    auto* self = static_cast<PipeStream*>(req->data);
    if (self->closing || self->closed)
        return;
    if (status != 0)
    {
        self->FailConnect(std::string("connect: ") + uv_strerror(status));
        return;
    }
    self->open = true;
    self->connectPending = false;
    if (self->connectCo.IsValid())
    {
        Lode::State vm(self->mainL);
        auto res = self->connectCo.Resume({});
        if (res.IsError() && Lode::Task::IsMainThread(vm, self->connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        self->connectCo = Lode::Coroutine();
    }
    self->StartReading();
}

void PipeStream::FailConnect(const std::string& message)
{
    if (closing)
        return;
    closing = true;
    if (connectPending && connectCo.IsValid())
    {
        Lode::State vm(mainL);
        auto res = connectCo.ResumeError(message);
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }
    connectPending = false;
    if (pipeInited && !pipeClosed)
    {
        pipeClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void PipeStream::AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle;
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void PipeStream::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<PipeStream*>(stream->data);
    self->CheckYield(nread, buf);
    if (buf->base)
        delete[] buf->base;
}

void PipeStream::OnWritten(uv_write_t* req, int status)
{
    auto* wreq = reinterpret_cast<WriteRequest*>(req);
    if (status != 0)
    {
        auto* self = static_cast<PipeStream*>(req->data);
        if (self)
            self->FireError(std::string("write: ") + uv_strerror(status));
    }
    delete wreq;
}

Lode::Value WrapPipeStream(Lode::State& vm, const std::shared_ptr<PipeStream>& stream, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunction([stream, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        std::string key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "DataReceived")
            return stream->dataProxy;
        if (key == "EndOfStream")
            return stream->endProxy;
        if (key == "ErrorOccurred")
            return stream->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("pipe: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("PipeStream")));
    meta.Set("__tostring", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(std::string("PipeStream"));
    }));
    Lode::ObjectWrap<PipeStream>::Wrap(vm, stream, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodepipe