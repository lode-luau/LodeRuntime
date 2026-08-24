// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Pipe/PipeServer.hpp"
#include "Pipe/PipeStream.hpp"
#include "Pipe/PipeHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Task.hpp"
#include <cstring>

namespace lodepipe
{

void PipeServer::InitSignals(Lode::State& vm)
{
    clientSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    clientProxy = clientSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void PipeServer::FireError(const std::string& message)
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

void PipeServer::ListenNative(const std::string& path)
{
    if (closing || closed || listening)
        return;
    std::memset(&pipe, 0, sizeof(pipe));
    pipeInited = true;
    pipe.data = this;
    int r = uv_pipe_init(loop, &pipe, 0);
    if (r != 0)
    {
        FireError(std::string("pipe init: ") + uv_strerror(r));
        return;
    }
    r = uv_pipe_bind(&pipe, path.c_str());
    if (r != 0)
    {
        FireError(std::string("bind: ") + uv_strerror(r));
        RequestClose();
        return;
    }
    r = uv_listen(reinterpret_cast<uv_stream_t*>(&pipe), backlog, OnConnection);
    if (r != 0)
    {
        FireError(std::string("listen: ") + uv_strerror(r));
        RequestClose();
        return;
    }
    listening = true;
}

Lode::Value PipeServer::MethodListen(Lode::State& vm, Lode::StackArgs args)
{
    if (closing || closed)
    {
        vm.RaiseError("pipe Server: server is closed");
        return Lode::Value();
    }
    if (listening)
    {
        vm.RaiseError("pipe Server: already listening");
        return Lode::Value();
    }
    if (args.Size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("pipe Server: path must be a string");
        return Lode::Value();
    }
    std::string raw = args[1].AsString();
    if (raw.empty())
    {
        vm.RaiseError("pipe Server: path must not be empty");
        return Lode::Value();
    }
    std::string path = NormalizePipePath(raw);

    std::memset(&pipe, 0, sizeof(pipe));
    pipe.data = this;
    int r = uv_pipe_init(loop, &pipe, 0);
    if (r != 0)
    {
        // Keep pipeInited false so no later uv_close runs over a zeroed handle.
        vm.RaiseError("pipe Server: " + std::string(uv_strerror(r)));
        return Lode::Value();
    }
    pipeInited = true;
    r = uv_pipe_bind(&pipe, path.c_str());
    if (r != 0)
    {
        // The handle is live at this point: close it before raising, or it
        // leaks and keeps the event loop alive (mirrors TCP BindFail).
        pipeClosed = true;
        closing = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
        vm.RaiseError("pipe Server: " + std::string(uv_strerror(r)));
        return Lode::Value();
    }
    r = uv_listen(reinterpret_cast<uv_stream_t*>(&pipe), backlog, OnConnection);
    if (r != 0)
    {
        pipeClosed = true;
        closing = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
        vm.RaiseError("pipe Server: " + std::string(uv_strerror(r)));
        return Lode::Value();
    }
    listening = true;
    return Lode::Value();
}

Lode::Value PipeServer::MethodAccept(Lode::State& vm)
{
    if (closing || closed)
    {
        vm.RaiseError("pipe Server: server is closed");
        return Lode::Value();
    }
    if (!listening)
    {
        vm.RaiseError("pipe Server: not listening");
        return Lode::Value();
    }
    if (acceptCo.IsValid())
    {
        vm.RaiseError("pipe Server: an Accept is already pending");
        return Lode::Value();
    }
    if (!pendingAccepts.empty())
    {
        while (!pendingAccepts.empty())
        {
            auto stream = pendingAccepts.front().lock();
            pendingAccepts.pop_front();
            if (stream && !stream->closed && !stream->closing)
                return WrapPipeStream(vm, stream, mgr->streamMethods);
            // Expired or closed entries are skipped.
        }
    }
    acceptCo = Lode::Coroutine(vm.GetLuaState());
    return vm.YieldThread();
}

void PipeServer::DeliverClient(const std::shared_ptr<PipeStream>& stream)
{
    if (acceptCo.IsValid())
    {
        Lode::State vm(mainL);
        Lode::Value clientValue = WrapPipeStream(vm, stream, mgr->streamMethods);
        auto co = acceptCo;
        acceptCo = Lode::Coroutine();
        auto res = co.Resume({ clientValue });
        if (res.IsError() && Lode::Task::IsMainThread(vm, co.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        return;
    }
    if (cppOnClient)
    {
        cppOnClient(stream);
        return;
    }
    pendingAccepts.push_back(stream);
    Lode::State vm(mainL);
    Lode::Value clientValue = WrapPipeStream(vm, stream, mgr->streamMethods);
    clientSig->Fire(clientValue);
}

void PipeServer::RequestClose()
{
    if (closing)
        return;
    closing = true;
    if (acceptCo.IsValid())
    {
        Lode::State vm(mainL);
        auto res = acceptCo.ResumeError("server closed");
        if (res.IsError() && Lode::Task::IsMainThread(vm, acceptCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        acceptCo = Lode::Coroutine();
    }
    if (listening && pipeInited && !pipeClosed)
    {
        pipeClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&pipe), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void PipeServer::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    listening = false;
    mgr->RemoveServer(shared_from_this());
    selfGuard.reset();
}

void PipeServer::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<PipeServer*>(handle->data);
    self->FinishClosed();
}

void PipeServer::OnConnection(uv_stream_t* server, int status)
{
    auto* self = static_cast<PipeServer*>(server->data);
    if (self->closing || self->closed)
        return;
    if (status != 0)
    {
        self->FireError(std::string("accept: ") + uv_strerror(status));
        return;
    }
    Lode::State vm(self->mainL);
    auto stream = std::make_shared<PipeStream>();
    stream->mgr = self->mgr;
    stream->mainL = self->mainL;
    stream->loop = self->loop;
    stream->pipeInited = true;
    std::memset(&stream->pipe, 0, sizeof(stream->pipe));
    stream->pipe.data = stream.get();
    int r = uv_pipe_init(self->loop, &stream->pipe, 0);
    if (r != 0)
    {
        stream->pipeClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&stream->pipe), PipeStream::OnHandleClosed);
        self->FireError(std::string("pipe init: ") + uv_strerror(r));
        return;
    }
    stream->InitSignals(vm);
    self->mgr->AddStream(stream);
    stream->selfGuard = stream;
    r = uv_accept(server, reinterpret_cast<uv_stream_t*>(&stream->pipe));
    if (r != 0)
    {
        stream->RequestClose();
        self->FireError(std::string("accept: ") + uv_strerror(r));
        return;
    }
    stream->open = true;
    stream->StartReading();
    self->DeliverClient(stream);
}

Lode::Value WrapPipeServer(Lode::State& vm, const std::shared_ptr<PipeServer>& server, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunction([server, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        std::string key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "ConnectionReceived")
            return server->clientProxy;
        if (key == "ErrorOccurred")
            return server->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFastFunction([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("pipe: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("PipeServer")));
    meta.Set("__tostring", vm.CreateFastFunction([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(std::string("PipeServer"));
    }));
    Lode::ObjectWrap<PipeServer>::Wrap(vm, server, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodepipe