// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tcp/Resolve.hpp"
#include "Tcp/TcpHelpers.hpp"
#include "Lode/Task.hpp"
#include "uv.h"
#include <cstring>

namespace lodetcp
{

struct ResolveState
{
    std::shared_ptr<TcpManager> mgr;
    uv_getaddrinfo_t req{};
    bool done = false;
    Lode::Coroutine co;
    Lode::Value callback;
};

std::string ResolveAddress(const struct addrinfo* res)
{
    struct sockaddr_storage addr;
    std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
    return FormatIpAddress(reinterpret_cast<const struct sockaddr*>(&addr));
}

void OnResolveDone(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
    auto* state = static_cast<ResolveState*>(req->data);
    if (state->done)
    {
        if (res)
            uv_freeaddrinfo(res);
        return;
    }
    state->done = true;

    std::string address;
    std::string error;
    if (status == 0 && res)
    {
        address = ResolveAddress(res);
        uv_freeaddrinfo(res);
    }
    else
    {
        error = status != 0 ? std::string(uv_strerror(status)) : "dns: no address found";
        if (res)
            uv_freeaddrinfo(res);
    }

    if (state->mgr->shuttingDown)
    {
        delete state;
        return;
    }

    Lode::State vm(state->mgr->mainL);
    if (state->co.IsValid())
    {
        if (error.empty())
        {
            auto result = state->co.Resume({Lode::Value(address)});
            if (result.IsError() && Lode::Task::IsMainThread(vm, state->co.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, result.GetError().ErrorMessage());
        }
        else
        {
            auto result = state->co.ResumeError(error);
            if (result.IsError() && Lode::Task::IsMainThread(vm, state->co.GetThreadState()))
                Lode::Task::SetMainThreadError(vm, result.GetError().ErrorMessage());
        }
    }
    else if (state->callback.IsFunction())
    {
        if (error.empty())
            Lode::Task::Spawn(vm, state->callback, {Lode::Value(address), Lode::Value()});
        else
            Lode::Task::Spawn(vm, state->callback, {Lode::Value(), Lode::Value(error)});
    }
    delete state;
}

int StartResolve(Lode::State& vm, const std::shared_ptr<TcpManager>& mgr, const std::string& host,
                 const Lode::Coroutine& co, const Lode::Value& callback)
{
    (void)vm;
    auto* state = new ResolveState();
    state->mgr = mgr;
    state->co = co;
    state->callback = callback;
    std::memset(&state->req, 0, sizeof(state->req));
    state->req.data = state;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int r = uv_getaddrinfo(mgr->loop, &state->req, OnResolveDone, host.c_str(), nullptr, &hints);
    if (r != 0)
    {
        delete state;
        return r;
    }
    return 0;
}

} // namespace lodetcp
