// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "FileSystem/FileWatcher.hpp"
#include "Lode/ObjectWrap.hpp"
#include <iostream>

namespace lodefs
{

FileWatcher::FileWatcher(std::shared_ptr<FsManager> m) : mgr(m)
{
    uv_fs_event_init(mgr->loop, &watcher);
    watcher.data = this;
}

FileWatcher::~FileWatcher()
{
}

void FileWatcher::RequestClose()
{
    if (closing || closed) return;
    closing = true;
    
    if (watching) {
        uv_fs_event_stop(&watcher);
        watching = false;
    }
    
    selfGuard = shared_from_this(); // Keep alive until close finishes
    uv_close(reinterpret_cast<uv_handle_t*>(&watcher), [](uv_handle_t* handle) {
        FileWatcher* self = static_cast<FileWatcher*>(handle->data);
        self->FinishClosed();
    });
}

void FileWatcher::FinishClosed()
{
    closed = true;
    selfGuard.reset();
}

void FileWatcher::CheckClosed()
{
}

void FileWatcher::OnEvent(uv_fs_event_t* handle, const char* filename, int events, int status)
{
    FileWatcher* self = static_cast<FileWatcher*>(handle->data);
    
    if (self->closing || self->closed) return;
    
    if (status < 0) {
        auto err = uv_strerror(status);
        if (self->errorOccurred) {
            self->errorOccurred->Fire({ Lode::Value(std::string("fs Watcher: ") + err) });
        }
        return;
    }
    
    std::string type = "";
    if (events & UV_RENAME) type = "rename";
    else if (events & UV_CHANGE) type = "change";
    
    std::string fileStr = filename ? filename : "";
    
    if (self->changed) {
        self->changed->Fire({ Lode::Value(type), Lode::Value(fileStr) });
    }
}

Lode::Value FileWatcher::MethodStart(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (watching) {
        vm.RaiseError("fs Watcher: Start called when already watching");
        return Lode::Value();
    }
    
    if (closed || closing) {
        vm.RaiseError("fs Watcher: watcher is closed");
        return Lode::Value();
    }
    
    bool recursive = false;
    if (args.size() > 1 && args[1].IsBoolean()) {
        recursive = args[1].AsBoolean();
    }
    
    std::string path = targetPath;
    
    int flags = 0;
    if (recursive) flags |= UV_FS_EVENT_RECURSIVE;
    
    int r = uv_fs_event_start(&watcher, OnEvent, path.c_str(), flags);
    if (r < 0) {
        vm.RaiseError(std::string("fs Watcher: Start: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    watching = true;
    selfGuard = shared_from_this(); // Keep alive while watching
    return Lode::Value();
}

Lode::Value FileWatcher::MethodStop(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing) {
        vm.RaiseError("fs Watcher: watcher is closed");
        return Lode::Value();
    }
    
    if (!watching) return Lode::Value();
    
    int r = uv_fs_event_stop(&watcher);
    if (r < 0) {
        vm.RaiseError(std::string("fs Watcher: Stop: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    watching = false;
    selfGuard.reset(); // Release watcher lock
    return Lode::Value();
}

Lode::Value WrapFileWatcher(Lode::State& vm, const std::shared_ptr<FileWatcher>& watcher, const Lode::Table& methods)
{
    Lode::Table t = vm.CreateTable();
    t.Set("__index", methods);
    
    watcher->changed = Lode::Signal::Create(vm);
    watcher->errorOccurred = Lode::Signal::Create(vm);
    
    t.Set("Changed", watcher->changed->CreatePublic());
    t.Set("ErrorOccurred", watcher->errorOccurred->CreatePublic());
    
    using Holder = std::shared_ptr<FileWatcher>;
    void* userMemory = vm.CreateUserdata(sizeof(Holder), [](void* ptr) {
        auto* h = static_cast<Holder*>(ptr);
        if (*h) {
            (*h)->RequestClose();
        }
        h->~Holder();
    });
    new (userMemory) Holder(watcher);
    vm.SetUserdataMetatable(-1, t);
    Lode::Value ud = vm.GetValue(-1);
    vm.Pop(1);
    return ud;
}

} // namespace lodefs
