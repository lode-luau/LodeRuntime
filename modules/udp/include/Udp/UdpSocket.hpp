// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "UdpExport.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <memory>
#include <string>
#include <vector>

namespace lodeudp
{

struct UDP_API UdpSocket : std::enable_shared_from_this<UdpSocket>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    uv_udp_t udp{};
    bool udpInited = false;
    bool udpClosed = false;
    bool closing = false;
    bool closed = false;
    bool reading = false;

    std::string localHost;
    int localPort = 0;

    std::shared_ptr<Lode::Signal> messageSig;
    std::shared_ptr<Lode::Signal> errorSig;

    Lode::Value messageProxy;
    Lode::Value errorProxy;

    std::shared_ptr<UdpSocket> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddress();
    void BindFail(Lode::State& vm, const std::string& message);
    void RequestClose();
    void FinishClosed();
    void StartReading();

    Lode::Value MethodBind(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodLocalAddress(Lode::State& vm);

    static int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen);
    static void OnHandleClosed(uv_handle_t* handle);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
    static void OnSend(uv_udp_send_t* req, int status);
};

UDP_API Lode::Value WrapUdpSocket(Lode::State& vm, const std::shared_ptr<UdpSocket>& socket, const Lode::Table& methods);

} // namespace lodeudp
