// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "WebSocketExport.hpp"
#include "WebSocketManager.hpp"
#include "WsHelpers.hpp"
#include "Tcp/TcpClient.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <memory>
#include <string>
#include <vector>

namespace lodews
{

struct WEBSOCKET_API WsClient : std::enable_shared_from_this<WsClient>
{
    std::shared_ptr<WebSocketManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    std::shared_ptr<lodetcp::TcpClient> tcpClient;

    uv_timer_t timer{};
    bool timerInited = false;
    bool timerClosed = false;

    bool serverSide = false;
    WsState state = WsState::Connecting;
    bool opened = false;
    bool closing = false;
    bool closed = false;
    bool disconnectedFired = false;
    bool connectPending = false;
    bool connectResumed = false;
    int closeCount = 0;

    Lode::Coroutine connectCo;
    ParsedWsUrl parsedUrl;
    uint64_t connectTimeoutMs = 0;
    std::vector<std::pair<std::string, std::string>> requestHeaders;

    std::string remoteHost;
    int remotePort = 0;
    std::string localHost;
    int localPort = 0;

    // Handshake
    std::vector<char> recvBuf;
    bool handshakeComplete = false;
    std::string sentKey;
    Lode::Value wrappedValue;
    std::shared_ptr<WsServer> ownerServer;

    // Frames
    uint8_t fragmentOpcode = 0;
    bool fragmentInProgress = false;
    std::vector<char> fragmentBuf;

    // Close
    bool closeSent = false;
    bool closeReceived = false;
    int closeCode = 0;
    std::string closeReason;
    bool hasCloseInfo = false;

    // Events
    std::shared_ptr<Lode::Signal> connectedSig;
    std::shared_ptr<Lode::Signal> messageSig;
    std::shared_ptr<Lode::Signal> disconnectedSig;
    std::shared_ptr<Lode::Signal> errorSig;

    Lode::Value connectedProxy;
    Lode::Value messageProxy;
    Lode::Value disconnectedProxy;
    Lode::Value errorProxy;

    std::shared_ptr<WsClient> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddresses();

    void NotifyConnectOk();
    void NotifyConnectError(const std::string& message);

    TimerMode timerMode = TimerMode::None;
    void StartTimer(uint64_t ms, TimerMode mode);
    void StopTimer();
    static void OnTimer(uv_timer_t* timer);

    void FailConnect(const std::string& message);

    void AttachTcpClient(std::shared_ptr<lodetcp::TcpClient> tcpClient);
    void OnTcpConnected();
    void OnTcpError(const std::string& err);
    void OnTcpMessage(const char* data, size_t size);
    void OnTcpDisconnected();

    void ProcessData();
    void DoClientHandshake(const std::string& headerBlock);
    void DoServerHandshake(const std::string& headerBlock);

    void ParseFrames();

    Lode::Value MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodClose(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodLocalAddress(Lode::State& vm);
    Lode::Value MethodRemoteAddress(Lode::State& vm);

    void SendRaw(const std::vector<char>& data);
    void SendFrame(uint8_t opcode, const std::vector<char>& payload);

    void CloseHandles();
    void RequestClose(uint16_t code = kCloseNormal, const std::string& reason = "");
    void CheckClosed();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
};

WEBSOCKET_API Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<WsClient>& client, const Lode::Table& methods);

} // namespace lodews
