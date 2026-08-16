// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sys/SysSignals.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include <csignal>

namespace lodesys
{

struct SignalWatcher : std::enable_shared_from_this<SignalWatcher>
{
    uv_signal_t handle{};
    std::shared_ptr<Lode::Signal> signal;
    bool initialized = false;
    bool closed = false;

    static void OnSignal(uv_signal_t* handle, int signum)
    {
        auto* watcher = static_cast<SignalWatcher*>(handle->data);
        if (watcher && watcher->signal)
            watcher->signal->Fire(Lode::Value(static_cast<double>(signum)));
    }

    static void OnClosed(uv_handle_t* handle)
    {
        auto* watcher = static_cast<SignalWatcher*>(handle->data);
        if (watcher)
            watcher->closed = true;
    }

    void Stop()
    {
        if (!initialized || closed)
            return;
        uv_signal_stop(&handle);
        uv_close(reinterpret_cast<uv_handle_t*>(&handle), OnClosed);
    }
};

static int ParseSignalName(const std::string& name)
{
    if (name == "SIGINT") return SIGINT;
    if (name == "SIGTERM") return SIGTERM;
#ifdef SIGHUP
    if (name == "SIGHUP") return SIGHUP;
#endif
#ifdef SIGWINCH
    if (name == "SIGWINCH") return SIGWINCH;
#endif
    return 0;
}

Lode::Value SignalManager::Watch(Lode::State& state, const std::vector<Lode::Value>& args)
{
    if (shuttingDown)
    {
        state.RaiseError("sys.Signal: runtime is shutting down");
        return Lode::Value();
    }
    if (args.empty() || !args[0].IsString())
    {
        state.RaiseError("sys.Signal: expected signal name");
        return Lode::Value();
    }
    std::string name = args[0].AsString();
    int signum = ParseSignalName(name);
    if (signum == 0)
    {
        state.RaiseError("sys.Signal: unsupported signal " + name);
        return Lode::Value();
    }

    auto it = watchers.find(name);
    if (it != watchers.end())
        return it->second->signal->CreatePublic();

    auto watcher = std::make_shared<SignalWatcher>();
    watcher->signal = Lode::Signal::Create(state);
    watcher->handle.data = watcher.get();
    int r = uv_signal_init(state.GetEventLoop().GetUVLoop(), &watcher->handle);
    if (r != 0)
    {
        state.RaiseError(std::string("sys.Signal: init failed: ") + uv_strerror(r));
        return Lode::Value();
    }
    watcher->initialized = true;
    r = uv_signal_start(&watcher->handle, SignalWatcher::OnSignal, signum);
    if (r != 0)
    {
        uv_close(reinterpret_cast<uv_handle_t*>(&watcher->handle), SignalWatcher::OnClosed);
        state.RaiseError(std::string("sys.Signal: start failed: ") + uv_strerror(r));
        return Lode::Value();
    }
    uv_unref(reinterpret_cast<uv_handle_t*>(&watcher->handle));
    watchers.emplace(name, watcher);
    return watcher->signal->CreatePublic();
}

void SignalManager::Shutdown()
{
    if (shuttingDown)
        return;
    shuttingDown = true;
    for (auto& entry : watchers)
        entry.second->Stop();
}

void BindSysSignals(Lode::State& vm, Lode::Table& exports)
{
    auto manager = std::make_shared<SignalManager>();
    manager->vm = &vm;
    Lode::Task::RegisterShutdownHook(vm, [manager]() { manager->Shutdown(); });
    exports.Set("Signal", vm.CreateFunction([manager](Lode::State& state, const std::vector<Lode::Value>& args) -> Lode::Value {
        return manager->Watch(state, args);
    }));
}

} // namespace lodesys
