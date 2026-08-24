// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PipeExport.hpp"
#include "PipeManager.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace lodepipe
{

struct PIPE_API PipeServer : std::enable_shared_from_this<PipeServer>
{
    std::shared_ptr<PipeManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_pipe_t pipe{};
    bool pipeInited = false;
    bool pipeClosed = false;
    bool listening = false;
    bool closing = false;
    bool closed = false;
    int backlog = 511;

    std::shared_ptr<Lode::Signal> clientSig;   // ConnectionReceived
    std::shared_ptr<Lode::Signal> errorSig;    // ErrorOccurred
    Lode::Value clientProxy;
    Lode::Value errorProxy;

    std::function<void(std::shared_ptr<PipeStream>)> cppOnClient;
    std::function<void(const std::string&)> cppOnError;

    // Hybrid accept: pending yield-based Accept() coroutine + queue of
    // accepted streams not yet consumed by Accept(). Weak pointers keep the
    // queue from immortalizing streams that were closed before being
    // consumed.
    Lode::Coroutine acceptCo;
    std::deque<std::weak_ptr<PipeStream>> pendingAccepts;

    std::shared_ptr<PipeServer> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void ListenNative(const std::string& path);
    void DeliverClient(const std::shared_ptr<PipeStream>& stream);

    Lode::Value MethodListen(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodAccept(Lode::State& vm);

    void RequestClose();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnection(uv_stream_t* server, int status);
};

PIPE_API Lode::Value WrapPipeServer(Lode::State& vm, const std::shared_ptr<PipeServer>& server, const Lode::Table& methods);

} // namespace lodepipe