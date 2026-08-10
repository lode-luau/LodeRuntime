// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "WebSocketExport.hpp"
#include "WebSocketManager.hpp"
#include "Tcp/TcpServer.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <memory>
#include <string>
#include <vector>

namespace lodews
{

struct WEBSOCKET_API WsServer : std::enable_shared_from_this<WsServer>
{
    std::shared_ptr<WebSocketManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    std::shared_ptr<lodetcp::TcpServer> tcpServer;

    bool listening = false;
    bool closing = false;
    bool closed = false;
    int closeCount = 0;

    std::string localHost;
    int localPort = 0;

    std::shared_ptr<Lode::Signal> clientSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value clientProxy;
    Lode::Value errorProxy;

    std::shared_ptr<WsServer> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddresses();

    void AttachTcpServer(std::shared_ptr<lodetcp::TcpServer> tcpServer);
    void OnTcpClient(std::shared_ptr<lodetcp::TcpClient> tcpClient);
    void OnTcpError(const std::string& err);

    Lode::Value MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodLocalAddress(Lode::State& vm);

    void RequestClose();
    void CheckClosed();
    void FinishClosed();
};

WEBSOCKET_API Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<WsServer>& server, const Lode::Table& methods);

} // namespace lodews
