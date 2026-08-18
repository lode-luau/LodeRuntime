// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/FsClasses.hpp"
#include "FileSystem/FileHandle.hpp"
#include "FileSystem/ReadStream.hpp"
#include "FileSystem/FileWatcher.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Task.hpp"
#include <iostream>

namespace lodefs
{

void BindClasses(Lode::State& vm, Lode::Exports& exports, std::shared_ptr<FsManager> mgr)
{
    // File namespace
    Lode::Table fileNs = vm.CreateTable();
    mgr->fileMethods = vm.CreateTable();
    mgr->fileMethods.Set("Read", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Read: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodRead(vm2, args);
    }));
    mgr->fileMethods.Set("ReadBuffer", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:ReadBuffer: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodReadBuffer(vm2, args);
    }));
    mgr->fileMethods.Set("Write", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Write: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodWrite(vm2, args);
    }));
    mgr->fileMethods.Set("Seek", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Seek: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodSeek(vm2, args);
    }));
    mgr->fileMethods.Set("Stat", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Stat: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodStat(vm2, args);
    }));
    mgr->fileMethods.Set("Sync", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Sync: invalid FileHandle"); return Lode::Value(); }
        return handle->MethodSync(vm2, args);
    }));
    mgr->fileMethods.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) { vm2.RaiseError("fs File:Close: invalid FileHandle"); return Lode::Value(); }
        handle->RequestClose();
        return Lode::Value();
    }));
    mgr->fileMethods.Set("IsOpen", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) {
        auto handle = Lode::ObjectWrap<lodefs::FileHandle>::Unwrap(vm2, 1);
        if (!handle) return Lode::Value(false);
        return Lode::Value(!handle->closed && !handle->closing);
    }));
    
    fileNs.Set("Open", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 2 || !args[1].IsString()) {
            vm2.RaiseError("fs File:Open: path must be a string");
            return Lode::Value();
        }
        std::string path = args[1].AsString();
        std::string mode = "r";
        if (args.size() > 2 && args[2].IsString()) mode = args[2].AsString();
        
        int flags = 0;
        if (mode == "r") flags = UV_FS_O_RDONLY;
        else if (mode == "w") flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (mode == "a") flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else if (mode == "r+") flags = UV_FS_O_RDWR;
        else if (mode == "w+") flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (mode == "a+") flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else {
            vm2.RaiseError("fs File:Open: invalid mode");
            return Lode::Value();
        }
        
        auto handle = std::make_shared<lodefs::FileHandle>();
        handle->mgr = mgr;
        handle->openFlags = flags;
        mgr->AddFile(handle);
        
        // Open file async and yield
        Lode::Coroutine coroutine = Lode::Coroutine(vm2.GetLuaState());
        
        int openMode = 0644;
        if (args.size() > 3 && args[3].IsNumber())
        {
            openMode = static_cast<int>(args[3].AsNumber());
        }

        struct OpenCtx {
            std::shared_ptr<lodefs::FileHandle> h;
            Lode::Coroutine co;
            lua_State* L;
            uv_fs_t req;
        };
        auto ctx = new OpenCtx();
        ctx->h = handle;
        ctx->co = coroutine;
        ctx->L = vm2.GetLuaState();
        ctx->req.data = ctx;
        
        int r = uv_fs_open(mgr->loop, &ctx->req, path.c_str(), flags, openMode, [](uv_fs_t* req) {
            auto ctx = static_cast<OpenCtx*>(req->data);
            if (ctx->h->mgr->shuttingDown) {
                if (req->result >= 0) {
                    ctx->h->fd = req->result;
                    ctx->h->isOpen = true;
                    ctx->h->RequestClose();
                }
                uv_fs_req_cleanup(req);
                delete ctx;
                return;
            }
            Lode::State vm(ctx->L);
            if (req->result < 0) {
                auto res = ctx->co.ResumeError(std::string("fs File:Open: ") + uv_strerror(req->result));
                if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->co.GetThreadState()))
                    Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
            } else {
                ctx->h->fd = req->result;
                ctx->h->isOpen = true;
                Lode::Value ud = lodefs::WrapFileHandle(vm, ctx->h, ctx->h->mgr->fileMethods);
                auto res = ctx->co.Resume({ ud });
                if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->co.GetThreadState()))
                    Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
            }
            uv_fs_req_cleanup(req);
            delete ctx;
        });
        
        if (r < 0) {
            delete ctx;
            vm2.RaiseError(std::string("fs File:Open: ") + uv_strerror(r));
            return Lode::Value();
        }
        
        vm2.YieldThread();
        return Lode::Value();
    }));
    exports.SetValue("File", Lode::Value(fileNs));
    
    // ReadStream namespace
    Lode::Table rsNs = vm.CreateTable();
    mgr->readStreamMethods = vm.CreateTable();
    mgr->readStreamMethods.Set("Start", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto stream = Lode::ObjectWrap<lodefs::ReadStream>::Unwrap(vm2, 1);
        if (!stream) { vm2.RaiseError("fs ReadStream:Start: invalid ReadStream"); return Lode::Value(); }
        return stream->MethodStart(vm2, args);
    }));
    mgr->readStreamMethods.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) {
        auto stream = Lode::ObjectWrap<lodefs::ReadStream>::Unwrap(vm2, 1);
        if (!stream) { vm2.RaiseError("fs ReadStream:Close: invalid ReadStream"); return Lode::Value(); }
        stream->RequestClose();
        return Lode::Value();
    }));
    rsNs.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 2 || !args[1].IsString()) {
            vm2.RaiseError("fs ReadStream:Create: path must be a string");
            return Lode::Value();
        }
        std::string path = args[1].AsString();
        
        // Open file async and yield
        Lode::Coroutine coroutine = Lode::Coroutine(vm2.GetLuaState());
        
        auto stream = std::make_shared<lodefs::ReadStream>(mgr);
        mgr->AddStream(stream);
        
        struct OpenCtx {
            std::shared_ptr<lodefs::ReadStream> s;
            Lode::Coroutine co;
            lua_State* L;
            uv_fs_t req;
        };
        auto ctx = new OpenCtx();
        ctx->s = stream;
        ctx->co = coroutine;
        ctx->L = vm2.GetLuaState();
        ctx->req.data = ctx;
        
        int r = uv_fs_open(mgr->loop, &ctx->req, path.c_str(), UV_FS_O_RDONLY, 0644, [](uv_fs_t* req) {
            auto ctx = static_cast<OpenCtx*>(req->data);
            if (ctx->s->mgr->shuttingDown) {
                if (req->result >= 0) {
                    ctx->s->fd = req->result;
                    ctx->s->RequestClose();
                }
                uv_fs_req_cleanup(req);
                delete ctx;
                return;
            }
            Lode::State vm(ctx->L);
            if (req->result < 0) {
                auto res = ctx->co.ResumeError(std::string("fs ReadStream:Create: ") + uv_strerror(req->result));
                if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->co.GetThreadState()))
                    Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
            } else {
                ctx->s->fd = req->result;
                Lode::Value ud = lodefs::WrapReadStream(vm, ctx->s, ctx->s->mgr->readStreamMethods);
                auto res = ctx->co.Resume({ ud });
                if (res.IsError() && Lode::Task::IsMainThread(vm, ctx->co.GetThreadState()))
                    Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
            }
            uv_fs_req_cleanup(req);
            delete ctx;
        });
        
        if (r < 0) {
            delete ctx;
            vm2.RaiseError(std::string("fs ReadStream:Create: ") + uv_strerror(r));
            return Lode::Value();
        }
        
        vm2.YieldThread();
        return Lode::Value();
    }));
    exports.SetValue("ReadStream", Lode::Value(rsNs));
    
    // Watcher namespace
    Lode::Table watcherNs = vm.CreateTable();
    mgr->watcherMethods = vm.CreateTable();
    mgr->watcherMethods.Set("Start", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto watcher = Lode::ObjectWrap<lodefs::FileWatcher>::Unwrap(vm2, 1);
        if (!watcher) { vm2.RaiseError("fs Watcher:Start: invalid FileWatcher"); return Lode::Value(); }
        return watcher->MethodStart(vm2, args);
    }));
    mgr->watcherMethods.Set("Stop", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) {
        auto watcher = Lode::ObjectWrap<lodefs::FileWatcher>::Unwrap(vm2, 1);
        if (!watcher) { vm2.RaiseError("fs Watcher:Stop: invalid FileWatcher"); return Lode::Value(); }
        return watcher->MethodStop(vm2, args);
    }));
    watcherNs.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 2 || !args[1].IsString()) {
            vm2.RaiseError("fs Watcher:Create: path must be a string");
            return Lode::Value();
        }
        std::string path = args[1].AsString();
        
        auto watcher = std::make_shared<lodefs::FileWatcher>(mgr);
        watcher->targetPath = path;
        mgr->AddWatcher(watcher);
        
        Lode::Value ud = lodefs::WrapFileWatcher(vm2, watcher, mgr->watcherMethods);
        
        return ud;
    }));
    exports.SetValue("Watcher", Lode::Value(watcherNs));
}

} // namespace lodefs
