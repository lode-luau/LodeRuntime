// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tty/TtyStream.hpp"
#include "Tty/TtyHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include <cstring>

namespace lodetty
{

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

void TtyStream::InitSignals(Lode::State& vm)
{
    dataSig = Lode::Signal::Create(vm);
    endSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    dataProxy = dataSig->CreatePublic();
    endProxy = endSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void TtyStream::FireError(const std::string& message)
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

std::string TtyStream::InitNative(int fd, bool readable)
{
    if (open || closing || closed)
        return "stream is already open or closed";
    this->readable = readable;
    std::memset(&tty, 0, sizeof(tty));
    ttyInited = true;
    tty.data = this;
    int r = uv_tty_init(loop, &tty, fd, readable ? 1 : 0);
    if (r != 0)
    {
        ttyInited = false;
        return std::string("tty init: ") + uv_strerror(r);
    }
    open = true;
    return "";
}

void TtyStream::StartReading()
{
    if (reading || closed || closing || !readable)
        return;
    reading = true;
    int r = uv_read_start(reinterpret_cast<uv_stream_t*>(&tty), AllocBuffer, OnRead);
    if (r != 0)
    {
        reading = false;
        FireError(std::string("read: ") + uv_strerror(r));
        RequestClose();
    }
}

void TtyStream::StopReading()
{
    if (!reading)
        return;
    reading = false;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&tty));
}

void TtyStream::WriteNative(const char* data, size_t size)
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
    int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&tty), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("write: ") + uv_strerror(r));
    }
}

Lode::Value TtyStream::MethodWrite(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("tty Write: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("tty Write: stream is not open");
        return Lode::Value();
    }
    if (args.size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
    {
        vm.RaiseError("tty Write: data must be a string or buffer");
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
    int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&tty), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("write: ") + uv_strerror(r));
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value TtyStream::MethodWriteLine(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("tty WriteLine: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("tty WriteLine: stream is not open");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("tty WriteLine: data must be a string");
        return Lode::Value();
    }
    std::string text = args[1].AsString();
    text += "\n";
    WriteNative(text.data(), text.size());
    return Lode::Value();
}

Lode::Value TtyStream::MethodRead(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("tty Read: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("tty Read: stream is not open");
        return Lode::Value();
    }
    if (!readable)
    {
        vm.RaiseError("tty Read: stream is not readable");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("tty Read: already reading");
        return Lode::Value();
    }
    int bytes = -1;
    if (args.size() > 1 && !args[1].IsNil())
    {
        if (!args[1].IsNumber())
        {
            vm.RaiseError("tty Read: bytes must be a number or nil");
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

Lode::Value TtyStream::MethodReadBuffer(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("tty ReadBuffer: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("tty ReadBuffer: stream is not open");
        return Lode::Value();
    }
    if (!readable)
    {
        vm.RaiseError("tty ReadBuffer: stream is not readable");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("tty Read: already reading");
        return Lode::Value();
    }
    int bytes = -1;
    if (args.size() > 1 && !args[1].IsNil())
    {
        if (!args[1].IsNumber())
        {
            vm.RaiseError("tty ReadBuffer: bytes must be a number or nil");
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

Lode::Value TtyStream::MethodReadLine(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    (void)args;
    if (closed || closing)
    {
        vm.RaiseError("tty ReadLine: stream is closed");
        return Lode::Value();
    }
    if (!open)
    {
        vm.RaiseError("tty ReadLine: stream is not open");
        return Lode::Value();
    }
    if (!readable)
    {
        vm.RaiseError("tty ReadLine: stream is not readable");
        return Lode::Value();
    }
    if (yieldThread)
    {
        vm.RaiseError("tty Read: already reading");
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

Lode::Value TtyStream::MethodStartStreaming(Lode::State& vm)
{
    if (!open)
    {
        vm.RaiseError("tty StartStreaming: stream is not open");
        return Lode::Value();
    }
    if (!readable)
    {
        vm.RaiseError("tty StartStreaming: stream is not readable");
        return Lode::Value();
    }
    StartReading();
    return Lode::Value();
}

Lode::Value TtyStream::MethodStopStreaming(Lode::State& vm)
{
    (void)vm;
    StopReading();
    return Lode::Value();
}

Lode::Value TtyStream::MethodGetWindowSize(Lode::State& vm)
{
    if (closed || closing || !open)
    {
        vm.RaiseError("tty GetWindowSize: stream is not open");
        return Lode::Value();
    }
    int w = 0, h = 0;
    uv_tty_get_winsize(&tty, &w, &h);
    Lode::Table result = vm.CreateTable();
    result.Set("width", Lode::Value(static_cast<double>(w)));
    result.Set("height", Lode::Value(static_cast<double>(h)));
    return Lode::Value(result);
}

Lode::Value TtyStream::MethodSetMode(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing || !open)
    {
        vm.RaiseError("tty SetMode: stream is not open");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsNumber())
    {
        vm.RaiseError("tty SetMode: mode must be a number");
        return Lode::Value();
    }
    int m = static_cast<int>(args[1].AsNumber());
    uv_tty_set_mode(&tty, static_cast<uv_tty_mode_t>(m));
    return Lode::Value();
}

void TtyStream::RequestClose()
{
    if (closing)
        return;
    closing = true;
    if (yieldThread)
        FinishYield(Lode::Value());
    if (reading)
        StopReading();
    if (ttyInited && !ttyClosed)
    {
        ttyClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&tty), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void TtyStream::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    open = false;
    if (endSig)
        endSig->Fire(Lode::Value());
    mgr->RemoveStream(shared_from_this());
    selfGuard.reset();
}

void TtyStream::CheckYield(ssize_t nread, const uv_buf_t* buf)
{
    if (nread < 0)
    {
        if (nread != UV_EOF)
            FireError(std::string("tty Read error: ") + uv_strerror(nread));
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

void TtyStream::FinishYield(const Lode::Value& val)
{
    if (!yieldThread)
        return;
    Lode::Coroutine co(yieldThread);
    yieldThread = nullptr;
    yieldingLine = false;
    StopReading();
    co.Resume({ val });
}

void TtyStream::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<TtyStream*>(handle->data);
    self->FinishClosed();
}

void TtyStream::AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle;
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void TtyStream::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<TtyStream*>(stream->data);
    self->CheckYield(nread, buf);
    if (buf->base)
        delete[] buf->base;
}

void TtyStream::OnWritten(uv_write_t* req, int status)
{
    auto* wreq = reinterpret_cast<WriteRequest*>(req);
    if (status != 0)
    {
        auto* self = static_cast<TtyStream*>(req->data);
        if (self)
            self->FireError(std::string("write: ") + uv_strerror(status));
    }
    delete wreq;
}

Lode::Value WrapTtyStream(Lode::State& vm, const std::shared_ptr<TtyStream>& stream, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([stream, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
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
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("tty: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("TtyStream")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("TtyStream"));
    }));
    Lode::ObjectWrap<TtyStream>::Wrap(vm, stream, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodetty
