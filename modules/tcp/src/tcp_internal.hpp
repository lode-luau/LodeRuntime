// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// tcp_internal.hpp - Internal declarations shared by the tcp implementation
// files (tcp_client.cpp, tcp_server.cpp, tcp_resolve.cpp, tcp_addr.cpp).
// NOT included by consumers; they only see tcp.h.
#ifndef LODE_TCP_INTERNAL_HPP
#define LODE_TCP_INTERNAL_HPP

#include "tcp.h"
#include "uv.h"

#include <string>

namespace tcpimpl
{

// Shared address helpers (tcp_addr.cpp). Return values mirror the uv
// conventions: 0 on success, a negative uv error code otherwise.
std::string FormatIpAddress(const struct sockaddr* addr);
std::string FormatSockAddr(const struct sockaddr* addr, int& portOut);
int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen);

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

struct TcpClient
{
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
    bool connectPending = false;
    int closeCount = 0;

    tcp_connect_cb onConnect = nullptr;
    tcp_data_cb onData = nullptr;
    tcp_close_cb onClose = nullptr;
    tcp_error_cb onError = nullptr;
    void* ctx = nullptr;

    // Lifetime: a consumer reference (from tcp_client_new / accept transfer)
    // plus an internal async reference held while uv work is active. The
    // object deletes itself when both drop to zero.
    int consumerRefs = 1;
    int asyncRefs = 0;

    std::string localHost;
    int localPort = 0;
    std::string remoteHost;
    int remotePort = 0;

    void GrabAsync() { ++asyncRefs; }
    void DropAsync();
    void MaybeDelete();

    void FireConnect();
    void FireData(const char* data, size_t len);
    void FireClose();
    void FireError(const std::string& message);

    int BeginConnect(const std::string& host, int port, uint64_t timeoutMs);
    void StartTcpConnect(const struct sockaddr* addr);
    void FailConnect(const std::string& message);

    void StartReading();
    void StopReading();
    int Write(const char* data, size_t len);
    void CloseHandles();
    void RequestClose();
    void CheckClosed();
    void FinishClosed();
    void UpdateAddresses();
    int LocalAddress(char* host, size_t cap, int* port) const;
    int RemoteAddress(char* host, size_t cap, int* port) const;

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
    static void OnConnected(uv_connect_t* req, int status);
    static void OnConnectTimeout(uv_timer_t* timer);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

struct TcpServer
{
    uv_loop_t* loop = nullptr;

    uv_tcp_t tcp{};
    bool tcpInited = false;
    bool tcpClosed = false;
    bool listening = false;
    bool closing = false;
    bool closed = false;

    tcp_accept_cb onAccept = nullptr;
    tcp_error_cb onError = nullptr;
    void* ctx = nullptr;

    int consumerRefs = 1;
    int asyncRefs = 0;

    std::string localHost;
    int localPort = 0;

    void GrabAsync() { ++asyncRefs; }
    void DropAsync();
    void MaybeDelete();

    void FireError(const std::string& message);
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnection(uv_stream_t* server, int status);
};

} // namespace tcpimpl

#endif
