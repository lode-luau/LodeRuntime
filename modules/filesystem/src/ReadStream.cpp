// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/ReadStream.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cstring>
#include <iostream>

namespace lodefs
{

ReadStream::ReadStream(std::shared_ptr<FsManager> m) : mgr(m)
{
    readReq.data = this;
}

ReadStream::~ReadStream()
{
}

void ReadStream::RequestClose()
{
    if (closing || closed) return;
    closing = true;
    
    if (fd != -1) {
        uv_fs_t* req = new uv_fs_t;
        req->data = this;
        selfGuard = shared_from_this(); // Keep alive
        uv_fs_close(mgr->loop, req, fd, [](uv_fs_t* r) {
            ReadStream* self = static_cast<ReadStream*>(r->data);
            self->fd = -1;
            uv_fs_req_cleanup(r);
            delete r;
            self->FinishClosed();
        });
    } else {
        FinishClosed();
    }
}

void ReadStream::FinishClosed()
{
    closed = true;
    selfGuard.reset();
}

void ReadStream::CheckClosed()
{
}

void ReadStream::OnRead(uv_fs_t* req)
{
    ReadStream* self = static_cast<ReadStream*>(req->data);
    
    if (self->closing || self->closed) {
        uv_fs_req_cleanup(req);
        return;
    }
    
    if (req->result < 0) {
        auto err = uv_strerror(req->result);
        if (self->errorOccurred) {
            self->errorOccurred->Fire({ Lode::Value(std::string("fs ReadStream: ") + err) });
        }
        uv_fs_req_cleanup(req);
        self->RequestClose();
        return;
    }
    
    if (req->result == 0) {
        // EOF
        if (self->endOfStream) {
            self->endOfStream->Fire();
        }
        uv_fs_req_cleanup(req);
        self->RequestClose();
        return;
    }
    
    // Emit buffer
    Lode::State vm(self->mgr->mainL);
    Lode::Value bufVal = vm.CreateBuffer(req->result);
    void* ptr = bufVal.AsBuffer(nullptr);
    if (ptr) std::memcpy(ptr, self->buffer, req->result);
    
    if (self->dataReceived) {
        self->dataReceived->Fire({ bufVal });
    }
    
    self->offset += req->result;
    uv_fs_req_cleanup(req);
    
    self->ContinueRead();
}

void ReadStream::ContinueRead()
{
    if (closing || closed) return;
    
    uv_buf_t iov = uv_buf_init(buffer, sizeof(buffer));
    int r = uv_fs_read(mgr->loop, &readReq, fd, &iov, 1, offset, OnRead);
    if (r < 0) {
        if (errorOccurred) {
            errorOccurred->Fire({ Lode::Value(std::string("fs ReadStream: ") + uv_strerror(r)) });
        }
        RequestClose();
    }
}

Lode::Value ReadStream::MethodStart(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (reading) {
        vm.RaiseError("fs ReadStream: Start called when already reading");
        return Lode::Value();
    }
    
    if (closed || closing) {
        vm.RaiseError("fs ReadStream: file is closed");
        return Lode::Value();
    }
    
    reading = true;
    selfGuard = shared_from_this(); // Keep alive while reading
    ContinueRead();
    return Lode::Value();
}

Lode::Value WrapReadStream(Lode::State& vm, const std::shared_ptr<ReadStream>& stream, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();

    stream->dataReceived = Lode::Signal::Create(vm);
    stream->endOfStream = Lode::Signal::Create(vm);
    stream->errorOccurred = Lode::Signal::Create(vm);
    stream->dataProxy = stream->dataReceived->CreatePublic();
    stream->endProxy = stream->endOfStream->CreatePublic();
    stream->errorProxy = stream->errorOccurred->CreatePublic();

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
        vm2.RaiseError("fs: ReadStream objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("ReadStream")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("ReadStream"));
    }));

    using Holder = std::shared_ptr<ReadStream>;
    void* userMemory = vm.CreateUserdata(sizeof(Holder), [](void* ptr) {
        auto* h = static_cast<Holder*>(ptr);
        if (*h) {
            (*h)->RequestClose();
        }
        h->~Holder();
    });
    new (userMemory) Holder(stream);
    vm.SetUserdataMetatable(-1, meta);
    Lode::Value ud = vm.GetValue(-1);
    vm.Pop(1);
    return ud;
}

} // namespace lodefs
