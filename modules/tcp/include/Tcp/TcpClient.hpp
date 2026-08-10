// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include "TcpManager.hpp"
#include "Lode/Coroutine.hpp"
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

struct TCP_API TcpClient : std::enable_shared_from_this<TcpClient>
{
    std::shared_ptr<TcpManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_tcp_t tcp{};
    uv_connect_t connReq{};
    uv_getaddrinfo_t addrReq{};
    uv_timer_t timer{};
    bool tcpInited = false;
    bool addrInited = false;
    bool timerInited = false;
    bool tcpClosed = false;
    bool timerClosed = false;

    bool connected = false;
    bool everConnected = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    bool disconnectedFired = false;
    bool connectPending = false;
    bool connectResumed = false;
    int closeCount = 0;

    Lode::Coroutine connectCo;
    std::string connectHost;
    int connectPort = 0;
    uint64_t connectTimeoutMs = 0;

    std::string remoteHost;
    int remotePort = 0;
    std::string localHost;
    int localPort = 0;

    std::shared_ptr<Lode::Signal> connectedSig;
    std::shared_ptr<Lode::Signal> messageSig;
    std::shared_ptr<Lode::Signal> disconnectedSig;
    std::shared_ptr<Lode::Signal> errorSig;

    Lode::Value connectedProxy;
    Lode::Value messageProxy;
    Lode::Value disconnectedProxy;
    Lode::Value errorProxy;

    std::shared_ptr<TcpClient> selfGuard;

    std::function<void()> cppOnConnected;
    std::function<void(const std::string& error)> cppOnError;
    std::function<void(const char* data, size_t size)> cppOnMessage;
    std::function<void()> cppOnDisconnected;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddresses();
    void NotifyConnectOk();
    void NotifyConnectError(const std::string& message);
    void StartReading();
    void StartTcpConnect(const struct sockaddr* addr);
    int BeginConnect();

    int ConnectNative(const std::string& host, int port, uint64_t timeoutMs);
    void SendNative(const char* data, size_t size);

    Lode::Value MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodLocalAddress(Lode::State& vm);
    Lode::Value MethodRemoteAddress(Lode::State& vm);

    void CloseHandles();
    void RequestClose();
    void FailConnect(const std::string& message);
    void CheckClosed();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
    static void OnConnected(uv_connect_t* req, int status);
    static void OnConnectTimeout(uv_timer_t* timer);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

TCP_API Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<TcpClient>& client, const Lode::Table& methods);

} // namespace lodetcp
