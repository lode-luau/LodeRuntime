// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Stdio/StdioStream.hpp"
#include "Stdio/StdioHelpers.hpp"
#include "stdio_internal.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include <cstring>
#include <algorithm>

namespace lodestdio
{

void StdioStream::InitSignals(Lode::State& vm)
{
    dataSig = Lode::Signal::Create(vm);
    endSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    resizeSig = Lode::Signal::Create(vm);

    dataProxy = dataSig->CreatePublic();
    endProxy = endSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
    resizeProxy = resizeSig->CreatePublic();
}

void StdioStream::FireError(const std::string& message)
{
    if (!mgr || mgr->shuttingDown || closed || closing)
        return;
    if (cppOnError)
    {
        cppOnError(message);
        return;
    }
    errorSig->Fire(Lode::Value(message));
}

std::string StdioStream::InitNative(int fd, bool readable)
{
    if (open || closing || closed)
        return "stream is already open or closed";

    this->fd = fd;
    this->readable = readable;
    handleType = uv_guess_handle(static_cast<uv_file>(fd));

    if (handleType == UV_TTY)
    {
        std::memset(&ttyHandle, 0, sizeof(ttyHandle));
        ttyHandle.data = this;
        int r = uv_tty_init(loop, &ttyHandle, fd, readable ? 1 : 0);
        if (r != 0)
        {
            return std::string("stdio tty init: ") + uv_strerror(r);
        }
        streamHandle = reinterpret_cast<uv_stream_t*>(&ttyHandle);
        handleInited = true;
    }
    else if (handleType == UV_NAMED_PIPE)
    {
        std::memset(&pipeHandle, 0, sizeof(pipeHandle));
        pipeHandle.data = this;
        int r = uv_pipe_init(loop, &pipeHandle, 0);
        if (r != 0)
        {
            return std::string("stdio pipe init: ") + uv_strerror(r);
        }
        r = uv_pipe_open(&pipeHandle, static_cast<uv_file>(fd));
        if (r != 0)
        {
            return std::string("stdio pipe open: ") + uv_strerror(r);
        }
        streamHandle = reinterpret_cast<uv_stream_t*>(&pipeHandle);
        handleInited = true;
    }
    else
    {
        fileMode = true;
        fileHandle = static_cast<uv_file>(fd);
        handleInited = true;
    }

    open = true;
    return "";
}

void StdioStream::StartReading()
{
    if (reading || closed || closing || !readable || !open)
        return;

    reading = true;
    if (fileMode)
    {
        StartFileRead();
    }
    else if (streamHandle)
    {
        int r = uv_read_start(streamHandle, AllocBuffer, OnRead);
        if (r != 0)
        {
            reading = false;
            FireError(std::string("read: ") + uv_strerror(r));
        }
    }
}

void StdioStream::StopReading()
{
    if (!reading)
        return;
    reading = false;
    if (streamHandle)
    {
        uv_read_stop(streamHandle);
    }
}

void StdioStream::StartFileRead()
{
    if (!fileMode || !reading || closing || closed)
        return;

    auto* rr = new FileReadRequest();
    std::memset(&rr->req, 0, sizeof(rr->req));
    rr->stream = this;
    rr->buf.base = new char[4096];
    rr->buf.len = 4096;

    int r = uv_fs_read(loop, &rr->req, fileHandle, &rr->buf, 1, fileOffset, [](uv_fs_t* req) {
        auto* rr = static_cast<FileReadRequest*>(req->data ? req->data : req);
        auto* self = rr->stream;
        ssize_t nread = req->result;
        if (nread > 0 && self)
        {
            self->fileOffset += nread;
            self->readBuffer.insert(self->readBuffer.end(), rr->buf.base, rr->buf.base + nread);
        }
        uv_fs_req_cleanup(req);
        delete[] rr->buf.base;
        delete rr;

        if (!self || self->closing || self->closed)
            return;

        if (nread > 0)
            self->ProcessQueue(false);
        else
            self->ProcessQueue(true);

        if (self->reading && !self->pendingQueue.empty() && nread > 0)
            self->StartFileRead();
        else if (nread <= 0)
            self->reading = false;
    });

    if (r < 0)
    {
        delete[] rr->buf.base;
        delete rr;
        reading = false;
        FireError(std::string("file read: ") + uv_strerror(r));
    }
}

void StdioStream::WriteNative(const char* data, size_t size)
{
    if (closed || closing || !open || size == 0 || !data)
        return;

    if (fileMode)
    {
        auto* fwr = new FileWriteRequest();
        std::memset(&fwr->req, 0, sizeof(fwr->req));
        fwr->stream = this;
        fwr->buf.base = new char[size];
        fwr->buf.len = size;
        std::memcpy(fwr->buf.base, data, size);

        int r = uv_fs_write(loop, &fwr->req, fileHandle, &fwr->buf, 1, -1, [](uv_fs_t* req) {
            auto* fwr = reinterpret_cast<FileWriteRequest*>(req);
            delete[] fwr->buf.base;
            uv_fs_req_cleanup(req);
            delete fwr;
        });

        if (r < 0)
        {
            delete[] fwr->buf.base;
            delete fwr;
            FireError(std::string("write: ") + uv_strerror(r));
        }
        return;
    }

    if (!streamHandle)
        return;

    auto* wr = new WriteRequest();
    std::memset(&wr->req, 0, sizeof(wr->req));
    wr->req.data = wr;
    wr->stream = this;
    wr->data.assign(data, data + size);

    uv_buf_t buf;
    buf.base = wr->data.data();
    buf.len = wr->data.size();

    int r = uv_write(&wr->req, streamHandle, &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wr;
        FireError(std::string("write: ") + uv_strerror(r));
    }
}

bool StdioStream::TryResolve(Lode::State& vm, const PendingRead& req, Lode::Value& outVal)
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

    if (req.isInto)
    {
        if (readBuffer.empty())
            return false;
        size_t available = readBuffer.size();
        size_t toRead = (req.requestedBytes > 0) ? (std::min)(req.requestedBytes, available) : available;
        size_t bSize = 0;
        void* bPtr = req.targetBufferValue.AsBuffer(&bSize);
        if (bPtr && req.offset < bSize)
        {
            size_t maxWrite = bSize - req.offset;
            size_t finalLen = (std::min)(toRead, maxWrite);
            std::memcpy(static_cast<char*>(bPtr) + req.offset, readBuffer.data(), finalLen);
            readBuffer.erase(readBuffer.begin(), readBuffer.begin() + finalLen);
            outVal = Lode::Value(static_cast<double>(finalLen));
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

void StdioStream::ProcessQueue(bool isEof)
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
            else if (req.isInto)
            {
                result = Lode::Value(0.0);
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
            if (req.isYield && req.coroutine.IsValid())
            {
                std::vector<Lode::Value> resVals;
                if (!result.IsNil())
                    resVals.push_back(result);
                req.coroutine.Resume(resVals);
            }
            else if (req.isCallback && req.callback.IsFunction())
            {
                std::vector<Lode::Value> resVals;
                if (!result.IsNil())
                    resVals.push_back(result);
                Lode::Task::Spawn(vm, req.callback, resVals);
            }
        }
        else
        {
            break;
        }
    }
}

void StdioStream::QueueRequest(const PendingRead& req)
{
    pendingQueue.push_back(req);
    StartReading();
}

Lode::Value StdioStream::MethodWrite(Lode::State& vm, Lode::StackArgs args)
{
    if (!open || closed || closing)
    {
        vm.RaiseError("stdio Write: stream is closed or unavailable");
        return Lode::Value();
    }
    if (args.empty())
        return Lode::Value();

    size_t valIdx = (args.Size() > 1 && !args[0].IsString() && !args[0].IsBuffer()) ? 1 : 0;
    const auto& arg = args[valIdx];

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
        vm.RaiseError("stdio Write: expected string or buffer");
    }
    return Lode::Value();
}

Lode::Value StdioStream::MethodWriteLine(Lode::State& vm, Lode::StackArgs args)
{
    if (!open || closed || closing)
    {
        vm.RaiseError("stdio WriteLine: stream is closed or unavailable");
        return Lode::Value();
    }
    if (args.empty())
        return Lode::Value();

    size_t valIdx = (args.Size() > 1 && !args[0].IsString() && !args[0].IsBuffer()) ? 1 : 0;
    const auto& arg = args[valIdx];

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
        vm.RaiseError("stdio WriteLine: expected string or buffer");
    }
    return Lode::Value();
}

Lode::Value StdioStream::MethodRead(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio Read: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.isYield = true;
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

    QueueRequest(req);
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadBuffer(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadBuffer: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.isYield = true;
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

    QueueRequest(req);
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadLine(Lode::State& vm, Lode::StackArgs)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadLine: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    PendingRead req;
    req.isYield = true;
    req.coroutine = Lode::Coroutine(vm.GetLuaState());
    req.isLine = true;

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        return result;
    }

    QueueRequest(req);
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadInto(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadInto: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    size_t bufIdx = (args.Size() > 1 && !args[0].IsBuffer()) ? 1 : 0;
    if (args.Size() <= bufIdx || !args[bufIdx].IsBuffer())
    {
        vm.RaiseError("stdio ReadInto: expected buffer argument");
        return Lode::Value();
    }

    PendingRead req;
    req.isYield = true;
    req.coroutine = Lode::Coroutine(vm.GetLuaState());
    req.isInto = true;
    req.targetBufferValue = args[bufIdx].ToValue();

    if (args.Size() > bufIdx + 1 && args[bufIdx + 1].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[bufIdx + 1].AsNumber(), "buffer offset");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        req.offset = res.GetValue();
    }

    size_t bSize = 0;
    (void)args[bufIdx].AsBuffer(&bSize);
    size_t maxAvail = (bSize > req.offset) ? (bSize - req.offset) : 0;
    size_t length = maxAvail;

    if (args.Size() > bufIdx + 2 && args[bufIdx + 2].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[bufIdx + 2].AsNumber(), "read length");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        length = res.GetValue();
    }
    req.requestedBytes = (std::min)(length, maxAvail);

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        return result;
    }

    QueueRequest(req);
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadAsync(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadAsync: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    size_t fnIdx = (args.Size() > 1 && !args[0].IsFunction()) ? 1 : 0;
    if (args.Size() <= fnIdx || !args[fnIdx].IsFunction())
    {
        vm.RaiseError("stdio ReadAsync: expected callback function");
        return Lode::Value();
    }

    PendingRead req;
    req.isCallback = true;
    req.callback = args[fnIdx].ToValue();

    if (args.Size() > fnIdx + 1 && args[fnIdx + 1].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[fnIdx + 1].AsNumber(), "read length");
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
        std::vector<Lode::Value> cbArgs;
        if (!result.IsNil()) cbArgs.push_back(result);
        Lode::Task::Spawn(vm, req.callback, cbArgs);
        return Lode::Value();
    }

    QueueRequest(req);
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadBufferAsync(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadBufferAsync: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    size_t fnIdx = (args.Size() > 1 && !args[0].IsFunction()) ? 1 : 0;
    if (args.Size() <= fnIdx || !args[fnIdx].IsFunction())
    {
        vm.RaiseError("stdio ReadBufferAsync: expected callback function");
        return Lode::Value();
    }

    PendingRead req;
    req.isCallback = true;
    req.callback = args[fnIdx].ToValue();
    req.isBuffer = true;

    if (args.Size() > fnIdx + 1 && args[fnIdx + 1].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[fnIdx + 1].AsNumber(), "read length");
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
        std::vector<Lode::Value> cbArgs;
        if (!result.IsNil()) cbArgs.push_back(result);
        Lode::Task::Spawn(vm, req.callback, cbArgs);
        return Lode::Value();
    }

    QueueRequest(req);
    return Lode::Value();
}

Lode::Value StdioStream::MethodReadIntoAsync(Lode::State& vm, Lode::StackArgs args)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio ReadIntoAsync: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();

    size_t bIdx = (args.Size() > 2 && !args[0].IsBuffer()) ? 1 : 0;
    if (args.Size() <= bIdx + 1 || !args[bIdx].IsBuffer() || !args[bIdx + 1].IsFunction())
    {
        vm.RaiseError("stdio ReadIntoAsync: expected buffer and callback arguments");
        return Lode::Value();
    }

    PendingRead req;
    req.isCallback = true;
    req.targetBufferValue = args[bIdx].ToValue();
    req.callback = args[bIdx + 1].ToValue();
    req.isInto = true;

    if (args.Size() > bIdx + 2 && args[bIdx + 2].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[bIdx + 2].AsNumber(), "buffer offset");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        req.offset = res.GetValue();
    }

    size_t bSize = 0;
    (void)args[bIdx].AsBuffer(&bSize);
    size_t maxAvail = (bSize > req.offset) ? (bSize - req.offset) : 0;
    size_t length = maxAvail;

    if (args.Size() > bIdx + 3 && args[bIdx + 3].IsNumber())
    {
        auto res = Lode::Numeric::ToSize(args[bIdx + 3].AsNumber(), "read length");
        if (res.IsError())
        {
            vm.RaiseError(res.GetError().ErrorMessage());
            return Lode::Value();
        }
        length = res.GetValue();
    }
    req.requestedBytes = (std::min)(length, maxAvail);

    Lode::Value result;
    if (TryResolve(vm, req, result))
    {
        std::vector<Lode::Value> cbArgs;
        if (!result.IsNil()) cbArgs.push_back(result);
        Lode::Task::Spawn(vm, req.callback, cbArgs);
        return Lode::Value();
    }

    QueueRequest(req);
    return Lode::Value();
}

Lode::Value StdioStream::MethodStartStreaming(Lode::State& vm)
{
    if (!readable || !open || closed)
    {
        vm.RaiseError("stdio StartStreaming: stream is not readable");
        return Lode::Value();
    }
    mainL = vm.GetMainThread();
    StartReading();
    return Lode::Value();
}

Lode::Value StdioStream::MethodStopStreaming(Lode::State&)
{
    StopReading();
    return Lode::Value();
}

Lode::Value StdioStream::MethodGetWindowSize(Lode::State& vm)
{
    int width = 0, height = 0;
    if (handleType == UV_TTY && streamHandle)
    {
        uv_tty_get_winsize(reinterpret_cast<uv_tty_t*>(streamHandle), &width, &height);
    }

    Lode::Table t = vm.CreateTable();
    t.Set(1, Lode::Value(static_cast<double>(width)));
    t.Set(2, Lode::Value(static_cast<double>(height)));
    t.Set("width", Lode::Value(static_cast<double>(width)));
    t.Set("height", Lode::Value(static_cast<double>(height)));
    t.Set("columns", Lode::Value(static_cast<double>(width)));
    t.Set("rows", Lode::Value(static_cast<double>(height)));
    return Lode::Value(t);
}

Lode::Value StdioStream::MethodSetMode(Lode::State& vm, Lode::StackArgs args)
{
    if (handleType != UV_TTY || !streamHandle)
        return Lode::Value();

    size_t modeIdx = (args.Size() > 1 && (args[0].IsUserdata() || args[0].IsTable())) ? 1 : 0;
    if (args.Size() <= modeIdx)
        return Lode::Value();

    uv_tty_mode_t mode = UV_TTY_MODE_NORMAL;
    if (args[modeIdx].IsString())
    {
        std::string s = args[modeIdx].AsString();
        if (s == "raw") mode = UV_TTY_MODE_RAW;
        else if (s == "io") mode = UV_TTY_MODE_IO;
        else if (s == "normal") mode = UV_TTY_MODE_NORMAL;
        else
        {
            vm.RaiseError("stdio SetMode: invalid mode string (expected 'normal', 'raw', or 'io')");
            return Lode::Value();
        }
    }
    else if (args[modeIdx].IsNumber())
    {
        mode = static_cast<uv_tty_mode_t>(static_cast<int>(args[modeIdx].AsNumber()));
    }
    else if (args[modeIdx].IsBoolean())
    {
        mode = args[modeIdx].AsBoolean() ? UV_TTY_MODE_RAW : UV_TTY_MODE_NORMAL;
    }

    int r = uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(streamHandle), mode);
    if (r != 0)
    {
        FireError(std::string("set mode: ") + uv_strerror(r));
    }
    return Lode::Value();
}

Lode::Value StdioStream::MethodSetRawMode(Lode::State& vm, Lode::StackArgs args)
{
    if (handleType != UV_TTY || !streamHandle)
        return Lode::Value();

    size_t bIdx = (args.Size() > 1 && (args[0].IsUserdata() || args[0].IsTable())) ? 1 : 0;
    bool enable = (args.Size() > bIdx && args[bIdx].IsBoolean()) ? args[bIdx].AsBoolean() : false;

    uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(streamHandle), enable ? UV_TTY_MODE_RAW : UV_TTY_MODE_NORMAL);
    return Lode::Value();
}

Lode::Value StdioStream::MethodIsTTY(Lode::State&)
{
    return Lode::Value(handleType == UV_TTY);
}

void StdioStream::RequestClose()
{
    if (closed || closing)
        return;
    closing = true;
    StopReading();
    pendingQueue.clear();

    if (streamHandle)
    {
        uv_close(reinterpret_cast<uv_handle_t*>(streamHandle), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void StdioStream::FinishClosed()
{
    closing = false;
    closed = true;
    open = false;
    streamHandle = nullptr;
    if (mgr)
        mgr->RemoveStream(shared_from_this());
    selfGuard.reset();
}

void StdioStream::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<StdioStream*>(handle->data);
    if (self)
        self->FinishClosed();
}

void StdioStream::AllocBuffer(uv_handle_t*, size_t suggestedSize, uv_buf_t* buf)
{
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void StdioStream::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<StdioStream*>(stream->data);
    if (!self || self->closing || self->closed)
    {
        if (buf->base) delete[] buf->base;
        return;
    }

    if (nread > 0)
    {
        self->readBuffer.insert(self->readBuffer.end(), buf->base, buf->base + nread);

        if (self->cppOnData)
            self->cppOnData(buf->base, static_cast<size_t>(nread));

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
            if (self->cppOnEnd)
                self->cppOnEnd();
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

void StdioStream::OnWritten(uv_write_t* req, int status)
{
    auto* wr = static_cast<WriteRequest*>(req->data);
    if (status != 0 && wr && wr->stream)
    {
        wr->stream->FireError(std::string("write: ") + uv_strerror(status));
    }
    delete wr;
}

Lode::Value WrapStdioStream(Lode::State& vm, const std::shared_ptr<StdioStream>& stream, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunction([stream, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        std::string key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsString() : "";

        if (key == "DataReceived") return stream->dataProxy;
        if (key == "EndOfStream") return stream->endProxy;
        if (key == "ErrorOccurred") return stream->errorProxy;
        if (key == "WindowResized") return stream->resizeProxy;

        auto val = methods.Get(key);
        if (val.IsOk() && !val.GetValue().IsNil())
            return val.GetValue();

        if (!key.empty())
        {
            std::string altKey = key;
            if (altKey[0] >= 'a' && altKey[0] <= 'z')
                altKey[0] = static_cast<char>(altKey[0] - 'a' + 'A');
            else if (altKey[0] >= 'A' && altKey[0] <= 'Z')
                altKey[0] = static_cast<char>(altKey[0] - 'A' + 'a');

            auto altVal = methods.Get(altKey);
            if (altVal.IsOk() && !altVal.GetValue().IsNil())
                return altVal.GetValue();
        }

        return Lode::Value();
    }));

    meta.Set("__newindex", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("stdio: objects are read-only");
        return Lode::Value();
    }));

    meta.Set("__metatable", Lode::Value(std::string("StdioStream")));
    meta.Set("__tostring", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(std::string("StdioStream"));
    }));

    Lode::ObjectWrap<StdioStream>::Wrap(vm, stream, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodestdio
