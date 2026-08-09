// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Coroutine.hpp"
#include "Lode/Module.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Task.hpp"
#include "Lode/Value.hpp"
#include "Lode/EventLoop.hpp"

#include "uv.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

namespace
{

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct SocketManager;
struct TcpClient;
struct TcpServer;

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<TcpClient>& client, const Lode::Table& methods);
Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<TcpServer>& server, const Lode::Table& methods);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string FormatIpAddress(const struct sockaddr* addr)
{
    char host[64] = {0};
    if (addr->sa_family == AF_INET)
    {
        uv_ip4_name(reinterpret_cast<const struct sockaddr_in*>(addr), host, sizeof(host));
    }
    else if (addr->sa_family == AF_INET6)
    {
        uv_ip6_name(reinterpret_cast<const struct sockaddr_in6*>(addr), host, sizeof(host));
    }
    else
    {
        return "?";
    }
    return std::string(host);
}

std::string FormatSockAddr(const struct sockaddr* addr, int& portOut)
{
    portOut = 0;
    char host[64] = {0};
    if (addr->sa_family == AF_INET)
    {
        const auto* a = reinterpret_cast<const struct sockaddr_in*>(addr);
        uv_ip4_name(a, host, sizeof(host));
        portOut = ntohs(a->sin_port);
    }
    else if (addr->sa_family == AF_INET6)
    {
        const auto* a = reinterpret_cast<const struct sockaddr_in6*>(addr);
        uv_ip6_name(a, host, sizeof(host));
        portOut = ntohs(a->sin6_port);
    }
    else
    {
        return "?";
    }
    return std::string(host);
}

bool IsValidPort(double value, bool allowZero = false)
{
    double minPort = allowZero ? 0.0 : 1.0;
    return std::isfinite(value) && std::trunc(value) == value && value >= minPort && value <= 65535.0;
}

// ---------------------------------------------------------------------------
// SocketManager
// ---------------------------------------------------------------------------

struct SocketManager : std::enable_shared_from_this<SocketManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table clientMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<TcpClient>> clients;
    std::vector<std::shared_ptr<TcpServer>> servers;

    void AddClient(const std::shared_ptr<TcpClient>& client) { clients.push_back(client); }

    void AddServer(const std::shared_ptr<TcpServer>& server) { servers.push_back(server); }

    void RemoveClient(const std::shared_ptr<TcpClient>& client)
    {
        auto it = std::find(clients.begin(), clients.end(), client);
        if (it != clients.end())
            clients.erase(it);
    }

    void RemoveServer(const std::shared_ptr<TcpServer>& server)
    {
        auto it = std::find(servers.begin(), servers.end(), server);
        if (it != servers.end())
            servers.erase(it);
    }

    void Shutdown();
};

// ---------------------------------------------------------------------------
// TcpClient
// ---------------------------------------------------------------------------

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

struct TcpClient : std::enable_shared_from_this<TcpClient>
{
    std::shared_ptr<SocketManager> mgr;
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

    void InitSignals(Lode::State& vm)
    {
        connectedSig = Lode::Signal::Create(vm);
        messageSig = Lode::Signal::Create(vm);
        disconnectedSig = Lode::Signal::Create(vm);
        errorSig = Lode::Signal::Create(vm);
        connectedProxy = connectedSig->CreatePublic();
        messageProxy = messageSig->CreatePublic();
        disconnectedProxy = disconnectedSig->CreatePublic();
        errorProxy = errorSig->CreatePublic();
    }

    void FireError(const std::string& message)
    {
        if (mgr->shuttingDown || closed || closing)
            return;
        errorSig->Fire(Lode::Value(message));
    }

    void UpdateAddresses()
    {
        struct sockaddr_storage addr;
        int namelen = static_cast<int>(sizeof(addr));
        if (uv_tcp_getsockname(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
        {
            localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
        }
        namelen = static_cast<int>(sizeof(addr));
        if (uv_tcp_getpeername(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
        {
            remoteHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), remotePort);
        }
    }

    void NotifyConnectOk()
    {
        if (!connectCo.IsValid())
            return;
        Lode::State vm(mainL);
        auto res = connectCo.Resume({});
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }

    void NotifyConnectError(const std::string& message)
    {
        if (!connectCo.IsValid())
            return;
        Lode::State vm(mainL);
        auto res = connectCo.ResumeError(message);
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }

    void StartReading()
    {
        if (reading || closed || closing)
            return;
        reading = true;
        int r = uv_read_start(reinterpret_cast<uv_stream_t*>(&tcp), AllocBuffer, OnRead);
        if (r != 0)
        {
            reading = false;
            FireError(std::string("read: ") + uv_strerror(r));
            RequestClose();
        }
    }

    void StartTcpConnect(const struct sockaddr* addr)
    {
        std::memset(&tcp, 0, sizeof(tcp));
        tcpInited = true;
        tcp.data = this;
        int r = uv_tcp_init(loop, &tcp);
        if (r != 0)
        {
            FailConnect(std::string("tcp: ") + uv_strerror(r));
            return;
        }
        std::memset(&connReq, 0, sizeof(connReq));
        connReq.data = this;
        r = uv_tcp_connect(&connReq, &tcp, addr, OnConnected);
        if (r != 0)
        {
            FailConnect(std::string("connect: ") + uv_strerror(r));
            return;
        }
    }

    int BeginConnect()
    {
        std::memset(&addrReq, 0, sizeof(addrReq));
        addrInited = true;
        addrReq.data = this;
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        std::string portStr = std::to_string(connectPort);
        int r = uv_getaddrinfo(loop, &addrReq, OnResolved, connectHost.c_str(), portStr.c_str(), &hints);
        if (r != 0)
        {
            addrInited = false;
            return r;
        }
        if (connectTimeoutMs > 0)
        {
            std::memset(&timer, 0, sizeof(timer));
            timer.data = this;
            int tr = uv_timer_init(loop, &timer);
            if (tr == 0)
            {
                timerInited = true;
                uv_timer_start(&timer, OnConnectTimeout, connectTimeoutMs, 0);
            }
            // A failed timer init just disables the connect timeout; the
            // connection attempt itself is unaffected.
        }
        return 0;
    }

    Lode::Value MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        // args: host, port, timeout?
        if (connected)
        {
            vm.RaiseError("socket Connect: already connected");
            return Lode::Value();
        }
        if (connectPending)
        {
            vm.RaiseError("socket Connect: a connection attempt is already in progress");
            return Lode::Value();
        }
        if (closing || closed)
        {
            vm.RaiseError("socket Connect: socket is closed");
            return Lode::Value();
        }
        if (args.size() < 3 || !args[1].IsString())
        {
            vm.RaiseError("socket Connect: host must be a string");
            return Lode::Value();
        }
        if (!args[2].IsNumber())
        {
            vm.RaiseError("socket Connect: port must be a number");
            return Lode::Value();
        }
        std::string host = args[1].AsString();
        if (host.empty())
        {
            vm.RaiseError("socket Connect: host must not be empty");
            return Lode::Value();
        }
        double portValue = args[2].AsNumber();
        if (!IsValidPort(portValue))
        {
            vm.RaiseError("socket Connect: port must be an integer between 1 and 65535");
            return Lode::Value();
        }
        uint64_t timeoutMs = 0;
        if (args.size() > 3 && !args[3].IsNil())
        {
            if (!args[3].IsNumber())
            {
                vm.RaiseError("socket Connect: timeout must be a number or nil");
                return Lode::Value();
            }
            auto ms = Lode::Numeric::ToMilliseconds(args[3].AsNumber(), 1.0, "timeout");
            if (ms.IsError())
            {
                vm.RaiseError(ms.GetError().ErrorMessage());
                return Lode::Value();
            }
            timeoutMs = ms.GetValue();
        }

        connectHost = host;
        connectPort = static_cast<int>(portValue);
        connectTimeoutMs = timeoutMs;
        connectPending = true;
        connectCo = Lode::Coroutine(vm.GetLuaState());

        int r = BeginConnect();
        if (r != 0)
        {
            connectPending = false;
            connectCo = Lode::Coroutine();
            vm.RaiseError("socket Connect: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return vm.YieldThread();
    }

    Lode::Value MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (closed || closing)
        {
            vm.RaiseError("socket Send: socket is closed");
            return Lode::Value();
        }
        if (!connected)
        {
            vm.RaiseError("socket Send: not connected");
            return Lode::Value();
        }
        if (args.size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
        {
            vm.RaiseError("socket Send: data must be a string or buffer");
            return Lode::Value();
        }
        std::vector<char> data;
        if (args[1].IsString())
        {
            const std::string& text = args[1].AsString();
            data.assign(text.begin(), text.end());
        }
        else
        {
            size_t size = 0;
            void* ptr = args[1].AsBuffer(&size);
            if (ptr && size > 0)
                data.assign(static_cast<const char*>(ptr), static_cast<const char*>(ptr) + size);
        }
        auto* wreq = new WriteRequest();
        std::memset(&wreq->req, 0, sizeof(wreq->req));
        wreq->req.data = this;
        wreq->data = std::move(data);
        uv_buf_t buf;
        buf.base = wreq->data.empty() ? const_cast<char*>("") : wreq->data.data();
        buf.len = wreq->data.size();
        int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&tcp), &buf, 1, OnWritten);
        if (r != 0)
        {
            delete wreq;
            FireError(std::string("write: ") + uv_strerror(r));
            return Lode::Value();
        }
        return Lode::Value();
    }

    Lode::Value MethodLocalAddress(Lode::State& vm)
    {
        if (!connected || closed)
        {
            vm.RaiseError("socket LocalAddress: not connected");
            return Lode::Value();
        }
        Lode::Table t = vm.CreateTable();
        t.Set("host", Lode::Value(localHost));
        t.Set("port", Lode::Value(static_cast<double>(localPort)));
        return Lode::Value(t);
    }

    Lode::Value MethodRemoteAddress(Lode::State& vm)
    {
        if (!connected || closed)
        {
            vm.RaiseError("socket RemoteAddress: not connected");
            return Lode::Value();
        }
        Lode::Table t = vm.CreateTable();
        t.Set("host", Lode::Value(remoteHost));
        t.Set("port", Lode::Value(static_cast<double>(remotePort)));
        return Lode::Value(t);
    }

    void CloseHandles()
    {
        if (closed)
            return;
        connectPending = false;
        if (reading)
        {
            uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp));
            reading = false;
        }
        if (timerInited && !timerClosed)
        {
            timerClosed = true;
            uv_timer_stop(&timer);
            uv_close(reinterpret_cast<uv_handle_t*>(&timer), OnHandleClosed);
            ++closeCount;
        }
        if (tcpInited && !tcpClosed)
        {
            tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
            ++closeCount;
        }
        CheckClosed();
    }

    void RequestClose()
    {
        if (closing)
            return;
        closing = true;
        if (connectPending && !connectResumed && !mgr->shuttingDown)
        {
            connectResumed = true;
            NotifyConnectError("connection closed");
        }
        connectPending = false;
        CloseHandles();
    }

    void FailConnect(const std::string& message)
    {
        if (closing)
            return;
        closing = true;
        if (connectPending && !connectResumed && connectCo.IsValid())
        {
            connectResumed = true;
            NotifyConnectError(message);
        }
        connectPending = false;
        CloseHandles();
    }

    void CheckClosed()
    {
        if (!closing || closeCount != 0 || addrInited)
            return;
        FinishClosed();
    }

    void FinishClosed()
    {
        if (closed)
            return;
        closed = true;
        if (!mgr->shuttingDown && everConnected && !disconnectedFired)
        {
            disconnectedFired = true;
            disconnectedSig->Fire();
        }
        mgr->RemoveClient(shared_from_this());
        selfGuard.reset();
    }

    // --- static uv callbacks ---

    static void OnHandleClosed(uv_handle_t* handle)
    {
        auto* self = static_cast<TcpClient*>(handle->data);
        self->closeCount -= 1;
        self->CheckClosed();
    }

    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
    {
        auto* self = static_cast<TcpClient*>(req->data);
        self->addrInited = false;
        if (self->closing)
        {
            if (res)
                uv_freeaddrinfo(res);
            self->CheckClosed();
            return;
        }
        if (status != 0)
        {
            if (res)
                uv_freeaddrinfo(res);
            self->FailConnect(std::string("dns: ") + uv_strerror(status));
            return;
        }
        struct sockaddr_storage addr;
        std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
        uv_freeaddrinfo(res);
        if (self->closing)
        {
            self->CheckClosed();
            return;
        }
        self->StartTcpConnect(reinterpret_cast<const struct sockaddr*>(&addr));
    }

    static void OnConnected(uv_connect_t* req, int status)
    {
        auto* self = static_cast<TcpClient*>(req->data);
        if (self->closing)
            return;
        if (status != 0)
        {
            self->FailConnect(std::string("connect: ") + uv_strerror(status));
            return;
        }
        self->connected = true;
        self->everConnected = true;
        self->connectPending = false;
        uv_tcp_nodelay(&self->tcp, 1);
        self->UpdateAddresses();
        if (self->timerInited && !self->timerClosed)
        {
            self->timerClosed = true;
            uv_timer_stop(&self->timer);
            uv_close(reinterpret_cast<uv_handle_t*>(&self->timer), OnHandleClosed);
            ++self->closeCount;
        }
        if (!self->connectResumed && self->connectCo.IsValid())
        {
            self->connectResumed = true;
            self->NotifyConnectOk();
        }
        if (self->closing)
            return;
        if (!self->mgr->shuttingDown)
            self->connectedSig->Fire();
        self->StartReading();
    }

    static void OnConnectTimeout(uv_timer_t* timer)
    {
        auto* self = static_cast<TcpClient*>(timer->data);
        if (self->closing)
            return;
        self->FailConnect("connect: connection timed out");
    }

    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
    {
        (void)handle;
        (void)suggestedSize;
        static constexpr size_t kChunkSize = 64 * 1024;
        buf->base = new char[kChunkSize];
        buf->len = kChunkSize;
    }

    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
    {
        auto* self = static_cast<TcpClient*>(stream->data);
        if (nread > 0)
        {
            if (!self->mgr->shuttingDown)
                self->messageSig->Fire(Lode::Value(std::string(buf->base, static_cast<size_t>(nread))));
        }
        else if (nread == UV_EOF)
        {
            self->RequestClose();
        }
        else if (nread < 0)
        {
            if (nread != UV_ECANCELED)
            {
                self->FireError(std::string("read: ") + uv_strerror(static_cast<int>(nread)));
                self->RequestClose();
            }
        }
        delete[] buf->base;
    }

    static void OnWritten(uv_write_t* req, int status)
    {
        auto* wreq = reinterpret_cast<WriteRequest*>(req);
        auto* self = static_cast<TcpClient*>(req->handle->data);
        if (status != 0 && !self->closing && !self->closed)
            self->FireError(std::string("write: ") + uv_strerror(status));
        delete wreq;
    }
};

// ---------------------------------------------------------------------------
// TcpServer
// ---------------------------------------------------------------------------

struct TcpServer : std::enable_shared_from_this<TcpServer>
{
    std::shared_ptr<SocketManager> mgr;
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

    void InitSignals(Lode::State& vm)
    {
        clientSig = Lode::Signal::Create(vm);
        errorSig = Lode::Signal::Create(vm);
        clientProxy = clientSig->CreatePublic();
        errorProxy = errorSig->CreatePublic();
    }

    void FireError(const std::string& message)
    {
        if (mgr->shuttingDown || closed || closing)
            return;
        errorSig->Fire(Lode::Value(message));
    }

    void UpdateAddresses()
    {
        struct sockaddr_storage addr;
        int namelen = static_cast<int>(sizeof(addr));
        if (uv_tcp_getsockname(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
        {
            localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
        }
    }

    Lode::Value MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (closing || closed)
        {
            vm.RaiseError("socket Server: server is closed");
            return Lode::Value();
        }
        if (listening)
        {
            vm.RaiseError("socket Server: already listening");
            return Lode::Value();
        }
        if (args.size() < 2 || !args[1].IsNumber())
        {
            vm.RaiseError("socket Server: port must be a number");
            return Lode::Value();
        }
        double portValue = args[1].AsNumber();
        if (!IsValidPort(portValue, true))
        {
            vm.RaiseError("socket Server: port must be an integer between 0 and 65535");
            return Lode::Value();
        }
        int port = static_cast<int>(portValue);
        std::string host;
        if (args.size() > 2 && !args[2].IsNil())
        {
            if (!args[2].IsString())
            {
                vm.RaiseError("socket Server: host must be a string or nil");
                return Lode::Value();
            }
            host = args[2].AsString();
        }

        std::memset(&tcp, 0, sizeof(tcp));
        tcpInited = true;
        tcp.data = this;
        int r = uv_tcp_init(loop, &tcp);
        if (r != 0)
        {
            BindFail(vm, std::string("tcp: ") + uv_strerror(r));
            return Lode::Value();
        }

        struct sockaddr_storage addr;
        int namelen = 0;
        r = MakeSockAddr(host, port, addr, namelen);
        if (r != 0)
        {
            BindFail(vm, "host must be an IPv4 or IPv6 address");
            return Lode::Value();
        }
        r = uv_tcp_bind(&tcp, reinterpret_cast<const struct sockaddr*>(&addr), 0);
        if (r != 0)
        {
            BindFail(vm, std::string("bind: ") + uv_strerror(r));
            return Lode::Value();
        }
        r = uv_listen(reinterpret_cast<uv_stream_t*>(&tcp), backlog, OnConnection);
        if (r != 0)
        {
            BindFail(vm, std::string("listen: ") + uv_strerror(r));
            return Lode::Value();
        }
        listening = true;
        UpdateAddresses();
        return Lode::Value();
    }

    Lode::Value MethodLocalAddress(Lode::State& vm)
    {
        if (!listening || closed)
        {
            vm.RaiseError("socket Server: not listening");
            return Lode::Value();
        }
        Lode::Table t = vm.CreateTable();
        t.Set("host", Lode::Value(localHost));
        t.Set("port", Lode::Value(static_cast<double>(localPort)));
        return Lode::Value(t);
    }

    void BindFail(Lode::State& vm, const std::string& message)
    {
        if (tcpInited && !tcpClosed)
        {
            tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
        }
        vm.RaiseError("socket Server: " + message);
    }

    void RequestClose()
    {
        if (closing)
            return;
        closing = true;
        if (listening && tcpInited && !tcpClosed)
        {
            tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
        }
        else
        {
            FinishClosed();
        }
    }

    void FinishClosed()
    {
        if (closed)
            return;
        closed = true;
        listening = false;
        mgr->RemoveServer(shared_from_this());
        selfGuard.reset();
    }

    static int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
    {
        if (host.find(':') != std::string::npos)
        {
            struct sockaddr_in6 a;
            std::memset(&a, 0, sizeof(a));
            int r = uv_ip6_addr(host.c_str(), port, &a);
            if (r != 0)
                return r;
            std::memcpy(&out, &a, sizeof(a));
            outLen = static_cast<int>(sizeof(a));
            return 0;
        }
        struct sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        std::string bindHost = host.empty() ? "0.0.0.0" : host;
        int r = uv_ip4_addr(bindHost.c_str(), port, &a);
        if (r != 0)
            return r;
        std::memcpy(&out, &a, sizeof(a));
        outLen = static_cast<int>(sizeof(a));
        return 0;
    }

    // --- static uv callbacks ---

    static void OnHandleClosed(uv_handle_t* handle)
    {
        auto* self = static_cast<TcpServer*>(handle->data);
        self->FinishClosed();
    }

    static void OnConnection(uv_stream_t* server, int status)
    {
        auto* self = static_cast<TcpServer*>(server->data);
        if (self->closing || self->closed)
            return;
        if (status != 0)
        {
            self->FireError(std::string("accept: ") + uv_strerror(status));
            return;
        }
        Lode::State vm(self->mainL);
        auto client = std::make_shared<TcpClient>();
        client->mgr = self->mgr;
        client->mainL = self->mainL;
        client->loop = self->loop;
        client->tcpInited = true;
        std::memset(&client->tcp, 0, sizeof(client->tcp));
        client->tcp.data = client.get();
        int r = uv_tcp_init(self->loop, &client->tcp);
        if (r != 0)
        {
            client->tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&client->tcp), TcpClient::OnHandleClosed);
            self->FireError(std::string("tcp init: ") + uv_strerror(r));
            return;
        }
        client->InitSignals(vm);
        self->mgr->AddClient(client);
        client->selfGuard = client;
        r = uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->tcp));
        if (r != 0)
        {
            client->RequestClose();
            self->FireError(std::string("accept: ") + uv_strerror(r));
            return;
        }
        client->connected = true;
        client->everConnected = true;
        uv_tcp_nodelay(&client->tcp, 1);
        client->UpdateAddresses();
        client->StartReading();

        Lode::Value clientValue = WrapClient(vm, client, self->mgr->clientMethods);
        self->clientSig->Fire(clientValue);
    }
};

// ---------------------------------------------------------------------------
// SocketManager Implementation
// ---------------------------------------------------------------------------

void SocketManager::Shutdown()
{
    shuttingDown = true;
    auto clientsCopy = clients;
    for (auto& client : clientsCopy)
        client->RequestClose();
    auto serversCopy = servers;
    for (auto& server : serversCopy)
        server->RequestClose();
}

// ---------------------------------------------------------------------------
// Class wrapping (metatables + __index)
// ---------------------------------------------------------------------------

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<TcpClient>& client, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([client, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "Connected")
            return client->connectedProxy;
        if (key == "MessageReceived")
            return client->messageProxy;
        if (key == "Disconnected")
            return client->disconnectedProxy;
        if (key == "ErrorOccurred")
            return client->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("socket: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("TcpSocket")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("TcpSocket"));
    }));
    Lode::ObjectWrap<TcpClient>::Wrap(vm, client, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<TcpServer>& server, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([server, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "ClientConnected")
            return server->clientProxy;
        if (key == "ErrorOccurred")
            return server->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("socket: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("Server")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("Server"));
    }));
    Lode::ObjectWrap<TcpServer>::Wrap(vm, server, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

// ---------------------------------------------------------------------------
// resolve / resolveAsync
// ---------------------------------------------------------------------------

struct ResolveState
{
    std::shared_ptr<SocketManager> mgr;
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

int StartResolve(Lode::State& vm, const std::shared_ptr<SocketManager>& mgr, const std::string& host,
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

// ---------------------------------------------------------------------------
// Method tables
// ---------------------------------------------------------------------------

Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<SocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Connect", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Connect: invalid TcpSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket Connect: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodConnect(vm2, args);
    }));

    m.Set("Send", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Send: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodSend(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Close: invalid TcpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Destroy: invalid TcpSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsConnected", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->connected && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket LocalAddress: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    m.Set("RemoteAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket RemoteAddress: invalid TcpSocket");
            return Lode::Value();
        }
        return self->MethodRemoteAddress(vm2);
    }));

    return m;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<SocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsListening", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpServer>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<TcpServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("socket Server: invalid Server");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Module entry
// ---------------------------------------------------------------------------

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<SocketManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->clientMethods = BuildClientMethods(vm, mgr);
    mgr->serverMethods = BuildServerMethods(vm, mgr);

    Lode::Table tcpSocketClass = vm.CreateTable();
    tcpSocketClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket: runtime is shutting down");
            return Lode::Value();
        }
        auto client = std::make_shared<TcpClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        return WrapClient(vm2, client, mgr->clientMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<TcpServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        if (args.size() > 1 && !args[1].IsNil())
        {
            if (!args[1].IsTable())
            {
                vm2.RaiseError("socket.Server:Create: opts must be a table or nil");
                return Lode::Value();
            }
            auto backlog = args[1].AsTable().Get("backlog");
            if (backlog.IsOk() && !backlog.GetValue().IsNil())
            {
                double value = backlog.GetValue().AsNumber();
                if (!IsValidPort(value))
                {
                    vm2.RaiseError("socket.Server:Create: backlog must be an integer between 1 and 65535");
                    return Lode::Value();
                }
                server->backlog = static_cast<int>(value);
            }
        }
        server->InitSignals(vm2);
        mgr->AddServer(server);
        server->selfGuard = server;
        return WrapServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);

    exports.SetTable("Server", serverClass);
    exports.SetTable("TcpSocket", tcpSocketClass);

    exports.Function("resolve", [mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 1 || !args[0].IsString())
        {
            vm2.RaiseError("socket.resolve: host must be a string");
            return Lode::Value();
        }
        std::string host = args[0].AsString();
        if (host.empty())
        {
            vm2.RaiseError("socket.resolve: host must not be empty");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket.resolve: runtime is shutting down");
            return Lode::Value();
        }
        int r = StartResolve(vm2, mgr, host, Lode::Coroutine(vm2.GetLuaState()), Lode::Value());
        if (r != 0)
        {
            vm2.RaiseError("socket.resolve: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return vm2.YieldThread();
    });

    exports.Function("resolveAsync", [mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() < 2 || !args[0].IsString() || !args[1].IsFunction())
        {
            vm2.RaiseError("socket.resolveAsync: expected host and callback");
            return Lode::Value();
        }
        std::string host = args[0].AsString();
        if (host.empty())
        {
            vm2.RaiseError("socket.resolveAsync: host must not be empty");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("socket.resolveAsync: runtime is shutting down");
            return Lode::Value();
        }
        int r = StartResolve(vm2, mgr, host, Lode::Coroutine(), args[1]);
        if (r != 0)
        {
            vm2.RaiseError("socket.resolveAsync: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return Lode::Value();
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}
