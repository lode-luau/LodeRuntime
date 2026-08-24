// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/FileHandle.hpp"
#include "Lode/Task.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#if defined(_WIN32)
#include <io.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif
namespace lodefs
{

namespace
{
constexpr size_t kMaxReadSize = 64ull * 1024 * 1024;

bool ParseReadSize(Lode::State& vm, Lode::StackArgs args, size_t& sizeOut)
{
    sizeOut = 65536;
    if (args.Size() <= 1)
        return true;
    if (!args[1].IsNumber())
    {
        vm.RaiseError("fs File: read size must be a number or nil");
        return false;
    }

    auto size = Lode::Numeric::ToSize(args[1].AsNumber(), "read size");
    if (size.IsError())
    {
        vm.RaiseError(size.GetError().ErrorMessage());
        return false;
    }
    if (size.GetValue() > kMaxReadSize)
    {
        vm.RaiseError("fs File: read size exceeds the 64 MiB per-call limit");
        return false;
    }
    sizeOut = size.GetValue();
    return true;
}
} // namespace

void FileHandle::RequestClose()
{
    if (closing || closed) return;
    closing = true;
    
    if (fd != -1) {
        uv_fs_t* req = new uv_fs_t;
        req->data = this;
        selfGuard = shared_from_this(); // Keep alive
        uv_fs_close(mgr->loop, req, fd, [](uv_fs_t* r) {
            FileHandle* self = static_cast<FileHandle*>(r->data);
            self->fd = -1;
            uv_fs_req_cleanup(r);
            delete r;
            self->FinishClosed();
        });
    } else {
        FinishClosed();
    }
}

void FileHandle::FinishClosed()
{
    closed = true;
    selfGuard.reset(); // Release
}

void FileHandle::CheckClosed()
{
    if (!closing && !closed) return;
    closeCount++;
    if (closeCount > 10) { // Safety loop for GC
        FinishClosed();
    }
}

struct FileHandleCtx {
    std::shared_ptr<FileHandle> handle;
    Lode::Coroutine coroutine;
    lua_State* L;
    uv_fs_t req;
    char* buffer = nullptr;
    size_t size = 0;
    bool isBuffer = false;
    std::string writeData;
    
    ~FileHandleCtx() {
        if (buffer) delete[] buffer;
        uv_fs_req_cleanup(&req);
    }
};

Lode::Value FileHandle::MethodRead(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing) {
        vm.RaiseError("fs File:Read: file is closed");
        return Lode::Value();
    }
    
    size_t sizeToRead = 0;
    if (!ParseReadSize(vm, args, sizeToRead)) return Lode::Value();
    
    auto ctx = new FileHandleCtx();
    ctx->handle = shared_from_this();
    ctx->L = vm.GetLuaState();
    ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
    ctx->buffer = new char[sizeToRead];
    ctx->size = sizeToRead;
    ctx->isBuffer = false;
    ctx->req.data = ctx;
    
    uv_buf_t iov = uv_buf_init(ctx->buffer, sizeToRead);
    
    int r = uv_fs_read(mgr->loop, &ctx->req, fd, &iov, 1, -1, [](uv_fs_t* req) {
        auto ctx = static_cast<FileHandleCtx*>(req->data);
        if (ctx->handle->mgr->shuttingDown) { delete ctx; return; }
        Lode::State vm(ctx->L);
        if (req->result < 0) {
            auto err = uv_strerror(req->result);
            auto res = ctx->coroutine.ResumeError(std::string("fs File:Read: ") + err);
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        } else {
            std::string data(ctx->buffer, req->result);
            auto res = ctx->coroutine.Resume({ Lode::Value(data) });
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        }
        delete ctx;
    });
    
    if (r < 0) {
        delete ctx;
        vm.RaiseError(std::string("fs File:Read: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value FileHandle::MethodReadBuffer(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing) {
        vm.RaiseError("fs File:ReadBuffer: file is closed");
        return Lode::Value();
    }
    
    size_t sizeToRead = 0;
    if (!ParseReadSize(vm, args, sizeToRead)) return Lode::Value();
    
    auto ctx = new FileHandleCtx();
    ctx->handle = shared_from_this();
    ctx->L = vm.GetLuaState();
    ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
    ctx->buffer = new char[sizeToRead];
    ctx->size = sizeToRead;
    ctx->isBuffer = true;
    ctx->req.data = ctx;
    
    uv_buf_t iov = uv_buf_init(ctx->buffer, sizeToRead);
    
    int r = uv_fs_read(mgr->loop, &ctx->req, fd, &iov, 1, -1, [](uv_fs_t* req) {
        auto ctx = static_cast<FileHandleCtx*>(req->data);
        if (ctx->handle->mgr->shuttingDown) { delete ctx; return; }
        Lode::State vm(ctx->L);
        if (req->result < 0) {
            auto err = uv_strerror(req->result);
            auto res = ctx->coroutine.ResumeError(std::string("fs File:ReadBuffer: ") + err);
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        } else {
            Lode::Value bufVal = vm.CreateBuffer(req->result);
            void* ptr = bufVal.AsBuffer(nullptr);
            if (ptr) std::memcpy(ptr, ctx->buffer, req->result);
            
            auto res = ctx->coroutine.Resume({ bufVal });
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        }
        delete ctx;
    });
    
    if (r < 0) {
        delete ctx;
        vm.RaiseError(std::string("fs File:ReadBuffer: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value FileHandle::MethodWrite(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing) {
        vm.RaiseError("fs File:Write: file is closed");
        return Lode::Value();
    }
    
    if (args.Size() < 2 || (!args[1].IsString() && !args[1].IsBuffer())) {
        vm.RaiseError("fs File:Write: data must be a string or buffer");
        return Lode::Value();
    }
    
    auto ctx = new FileHandleCtx();
    ctx->handle = shared_from_this();
    ctx->L = vm.GetLuaState();
    ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
    
    if (args[1].IsString()) {
        ctx->writeData = args[1].AsString();
    } else {
        size_t s = 0;
        void* p = args[1].AsBuffer(&s);
        if (p) ctx->writeData.assign(static_cast<const char*>(p), s);
    }
    
    ctx->req.data = ctx;
    
    uv_buf_t iov = uv_buf_init(const_cast<char*>(ctx->writeData.data()), ctx->writeData.size());
    
    int r = uv_fs_write(mgr->loop, &ctx->req, fd, &iov, 1, -1, [](uv_fs_t* req) {
        auto ctx = static_cast<FileHandleCtx*>(req->data);
        if (ctx->handle->mgr->shuttingDown) { delete ctx; return; }
        Lode::State vm(ctx->L);
        if (req->result < 0) {
            auto err = uv_strerror(req->result);
            auto res = ctx->coroutine.ResumeError(std::string("fs File:Write: ") + err);
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        } else {
            auto res = ctx->coroutine.Resume({});
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        }
        delete ctx;
    });
    
    if (r < 0) {
        delete ctx;
        vm.RaiseError(std::string("fs File:Write: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    vm.YieldThread();
    return Lode::Value();
}

namespace
{
struct SeekContext
{
    std::shared_ptr<FileHandle> handle;
    Lode::Coroutine coroutine;
    lua_State* L = nullptr;
    uv_work_t work{};
    int64_t offset = 0;
    int whence = SEEK_SET;
    int64_t result = -1;
    std::string error;
};

void SeekWork(uv_work_t* request)
{
    auto* context = static_cast<SeekContext*>(request->data);
#if defined(_WIN32)
    const __int64 result = _lseeki64(context->handle->fd, context->offset, context->whence);
#else
    const off_t result = lseek(context->handle->fd, static_cast<off_t>(context->offset), context->whence);
#endif
    if (result < 0)
    {
        context->error = std::strerror(errno);
        return;
    }

    if (static_cast<uint64_t>(result) > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()))
    {
        context->error = "resulting position is outside the supported integer range";
        return;
    }
    context->result = static_cast<int64_t>(result);
}

void SeekAfter(uv_work_t* request, int status)
{
    auto* context = static_cast<SeekContext*>(request->data);
    if (context->handle->mgr->shuttingDown)
    {
        delete context;
        return;
    }

    Lode::State vm(context->L);
    if (status < 0)
    {
        auto resumed = context->coroutine.ResumeError(std::string("fs File:Seek: ") + uv_strerror(status));
        if (resumed.IsError() && Lode::Task::IsMainThread(vm, context->coroutine.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, resumed.GetError().ErrorMessage());
    }
    else if (!context->error.empty())
    {
        auto resumed = context->coroutine.ResumeError(std::string("fs File:Seek: ") + context->error);
        if (resumed.IsError() && Lode::Task::IsMainThread(vm, context->coroutine.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, resumed.GetError().ErrorMessage());
    }
    else
    {
        auto resumed = context->coroutine.Resume({ Lode::Value(context->result) });
        if (resumed.IsError() && Lode::Task::IsMainThread(vm, context->coroutine.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, resumed.GetError().ErrorMessage());
    }
    delete context;
}
} // namespace

Lode::Value FileHandle::MethodSeek(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing)
    {
        vm.RaiseError("fs File:Seek: file is closed");
        return Lode::Value();
    }

    if (openFlags & UV_FS_O_APPEND)
    {
        vm.RaiseError("fs File:Seek: append-mode handles cannot be repositioned");
        return Lode::Value();
    }

    if (args.Size() < 2 || !args[1].IsNumber())
    {
        vm.RaiseError("fs File:Seek: offset must be an integer");
        return Lode::Value();
    }

    auto offset = args[1].TryAsInteger();
    if (offset.IsError())
    {
        vm.RaiseError("fs File:Seek: offset must be an integer");
        return Lode::Value();
    }

    int whence = SEEK_SET;
    if (args.Size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsString())
        {
            vm.RaiseError("fs File:Seek: whence must be 'set', 'cur', or 'end'");
            return Lode::Value();
        }

        const std::string mode = args[2].AsString();
        if (mode == "cur")
            whence = SEEK_CUR;
        else if (mode == "end")
            whence = SEEK_END;
        else if (mode != "set")
        {
            vm.RaiseError("fs File:Seek: whence must be 'set', 'cur', or 'end'");
            return Lode::Value();
        }
    }

    auto* context = new SeekContext();
    context->handle = shared_from_this();
    context->coroutine = Lode::Coroutine(vm.GetLuaState());
    context->L = vm.GetLuaState();
    context->offset = offset.GetValue();
    context->whence = whence;
    context->work.data = context;

    const int status = uv_queue_work(mgr->loop, &context->work, SeekWork, SeekAfter);
    if (status < 0)
    {
        delete context;
        vm.RaiseError(std::string("fs File:Seek: ") + uv_strerror(status));
        return Lode::Value();
    }

    vm.YieldThread();
    return Lode::Value();
}

Lode::Value FileHandle::MethodStat(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing) {
        vm.RaiseError("fs File:Stat: file is closed");
        return Lode::Value();
    }
    
    auto ctx = new FileHandleCtx();
    ctx->handle = shared_from_this();
    ctx->L = vm.GetLuaState();
    ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
    ctx->req.data = ctx;
    
    int r = uv_fs_fstat(mgr->loop, &ctx->req, fd, [](uv_fs_t* req) {
        auto ctx = static_cast<FileHandleCtx*>(req->data);
        if (ctx->handle->mgr->shuttingDown) { delete ctx; return; }
        Lode::State vm(ctx->L);
        if (req->result < 0) {
            auto err = uv_strerror(req->result);
            auto res = ctx->coroutine.ResumeError(std::string("fs File:Stat: ") + err);
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        } else {
            Lode::Table t = vm.CreateTable();
            t.Set("size", Lode::Value(static_cast<double>(req->statbuf.st_size)));
            t.Set("mtime", Lode::Value(req->statbuf.st_mtim.tv_sec + req->statbuf.st_mtim.tv_nsec / 1e9));
            t.Set("atime", Lode::Value(req->statbuf.st_atim.tv_sec + req->statbuf.st_atim.tv_nsec / 1e9));
            t.Set("ctime", Lode::Value(req->statbuf.st_ctim.tv_sec + req->statbuf.st_ctim.tv_nsec / 1e9));
            t.Set("isDirectory", Lode::Value((req->statbuf.st_mode & S_IFMT) == S_IFDIR));
            t.Set("isFile", Lode::Value((req->statbuf.st_mode & S_IFMT) == S_IFREG));
            
            auto res = ctx->coroutine.Resume({ Lode::Value(t) });
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        }
        delete ctx;
    });
    
    if (r < 0) {
        delete ctx;
        vm.RaiseError(std::string("fs File:Stat: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value FileHandle::MethodSync(Lode::State& vm, Lode::StackArgs args)
{
    if (closed || closing) {
        vm.RaiseError("fs File:Sync: file is closed");
        return Lode::Value();
    }
    
    auto ctx = new FileHandleCtx();
    ctx->handle = shared_from_this();
    ctx->L = vm.GetLuaState();
    ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
    ctx->req.data = ctx;
    
    int r = uv_fs_fsync(mgr->loop, &ctx->req, fd, [](uv_fs_t* req) {
        auto ctx = static_cast<FileHandleCtx*>(req->data);
        if (ctx->handle->mgr->shuttingDown) { delete ctx; return; }
        Lode::State vm(ctx->L);
        if (req->result < 0) {
            auto err = uv_strerror(req->result);
            auto res = ctx->coroutine.ResumeError(std::string("fs File:Sync: ") + err);
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        } else {
            auto res = ctx->coroutine.Resume({});
            if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        }
        delete ctx;
    });
    
    if (r < 0) {
        delete ctx;
        vm.RaiseError(std::string("fs File:Sync: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    vm.YieldThread();
    return Lode::Value();
}

Lode::Value WrapFileHandle(Lode::State& vm, const std::shared_ptr<FileHandle>& handle, const Lode::Table& methods)
{
    Lode::Table t = vm.CreateTable();
    t.Set("__index", methods);
    using Holder = std::shared_ptr<FileHandle>;
    void* userMemory = vm.CreateUserdata(sizeof(Holder), [](void* ptr) {
        auto* h = static_cast<Holder*>(ptr);
        if (*h) {
            (*h)->RequestClose();
        }
        h->~Holder();
    });
    new (userMemory) Holder(handle);
    vm.SetUserdataMetatable(-1, t);
    Lode::Value ud = vm.GetValue(-1);
    vm.Pop(1);
    return ud;
}

} // namespace lodefs
