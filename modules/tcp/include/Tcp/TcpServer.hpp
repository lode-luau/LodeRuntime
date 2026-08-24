// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include "TcpManager.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lodetcp
{

struct TCP_API TcpServer : std::enable_shared_from_this<TcpServer>
{
    std::shared_ptr<TcpManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_tcp_t tcp{};
    bool tcpInited = false;
    bool tcpClosed = false;
    bool listening = false;
    bool closing = false;
    bool closed = false;
    int backlog = 511;

    std::string localHost;
    int localPort = 0;

    std::shared_ptr<Lode::Signal> clientSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value clientProxy;
    Lode::Value errorProxy;

    std::shared_ptr<TcpServer> selfGuard;

    std::function<void(std::shared_ptr<TcpClient>)> cppOnClient;
    std::function<void(const std::string&)> cppOnError;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddresses();
    void BindFail(Lode::State& vm, const std::string& message);
    void BindFailNative(const std::string& message);

    void ListenNative(const std::string& host, int port);

    Lode::Value MethodListen(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodLocalAddress(Lode::State& vm);

    void RequestClose();
    void FinishClosed();

    static int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen);
    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnection(uv_stream_t* server, int status);
};

TCP_API Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<TcpServer>& server, const Lode::Table& methods);

} // namespace lodetcp
