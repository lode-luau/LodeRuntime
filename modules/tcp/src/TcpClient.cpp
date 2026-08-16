// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tcp/TcpClient.hpp"
#include "Tcp/TcpHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include <cstring>
#include <algorithm>

namespace lodetcp
{

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

void TcpClient::InitSignals(Lode::State& vm)
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

void TcpClient::FireError(const std::string& message)
{
    if (mgr->shuttingDown || closed || closing)
        return;
    if (cppOnError)
    {
        cppOnError(message);
        return;
    }
    errorSig->Fire(Lode::Value(message));
}

void TcpClient::UpdateAddresses()
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

void TcpClient::NotifyConnectOk()
{
    if (cppOnConnected)
    {
        cppOnConnected();
        return;
    }
    if (!connectCo.IsValid())
        return;
    Lode::State vm(mainL);
    auto res = connectCo.Resume({});
    if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
    connectCo = Lode::Coroutine();
}

void TcpClient::NotifyConnectError(const std::string& message)
{
    if (cppOnError && connectPending)
    {
        cppOnError(message);
        return;
    }
    if (!connectCo.IsValid())
        return;
    Lode::State vm(mainL);
    auto res = connectCo.ResumeError(message);
    if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
    connectCo = Lode::Coroutine();
}

void TcpClient::StartReading()
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

void TcpClient::StartTcpConnect(const struct sockaddr* addr)
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

int TcpClient::BeginConnect()
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
    }
    return 0;
}

int TcpClient::ConnectNative(const std::string& host, int port, uint64_t timeoutMs)
{
    if (connected || connectPending || closing || closed)
        return -1;
    connectHost = host;
    connectPort = port;
    connectTimeoutMs = timeoutMs;
    connectPending = true;
    int r = BeginConnect();
    if (r != 0)
    {
        connectPending = false;
    }
    return r;
}

Lode::Value TcpClient::MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args)
{
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
    if (args.size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("socket Connect: host must be a string");
        return Lode::Value();
    }
    if (args.size() < 3 || !args[2].IsNumber())
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

void TcpClient::SendNative(const char* data, size_t size)
{
    if (closed || closing || !connected)
        return;
    std::vector<char> vec;
    if (size > 0 && data)
        vec.assign(data, data + size);
    auto* wreq = new WriteRequest();
    std::memset(&wreq->req, 0, sizeof(wreq->req));
    wreq->req.data = this;
    wreq->data = std::move(vec);
    uv_buf_t buf;
    buf.base = wreq->data.empty() ? const_cast<char*>("") : wreq->data.data();
    buf.len = wreq->data.size();
    int r = uv_write(&wreq->req, reinterpret_cast<uv_stream_t*>(&tcp), &buf, 1, OnWritten);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("write: ") + uv_strerror(r));
    }
}

Lode::Value TcpClient::MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args)
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

Lode::Value TcpClient::MethodLocalAddress(Lode::State& vm)
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

Lode::Value TcpClient::MethodSetNoDelay(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (!connected || closed)
    {
        vm.RaiseError("socket SetNoDelay: not connected");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsBoolean())
    {
        vm.RaiseError("socket SetNoDelay: enabled must be a boolean");
        return Lode::Value();
    }
    if (uv_tcp_nodelay(&tcp, args[1].AsBoolean() ? 1 : 0) != 0)
    {
        vm.RaiseError("socket SetNoDelay: failed to apply nodelay");
    }
    return Lode::Value();
}

Lode::Value TcpClient::MethodSetKeepAlive(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (!connected || closed)
    {
        vm.RaiseError("socket SetKeepAlive: not connected");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsBoolean())
    {
        vm.RaiseError("socket SetKeepAlive: enabled must be a boolean");
        return Lode::Value();
    }
    unsigned int delay = 0;
    if (args.size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsNumber() || args[2].AsNumber() < 0)
        {
            vm.RaiseError("socket SetKeepAlive: delay must be a non-negative number of seconds");
            return Lode::Value();
        }
        delay = static_cast<unsigned int>(args[2].AsNumber());
    }
    if (args[1].AsBoolean() && delay == 0)
        delay = 60;
    if (uv_tcp_keepalive(&tcp, args[1].AsBoolean() ? 1 : 0, delay) != 0)
    {
        vm.RaiseError("socket SetKeepAlive: failed to apply keepalive");
    }
    return Lode::Value();
}

Lode::Value TcpClient::MethodRemoteAddress(Lode::State& vm)
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

void TcpClient::CloseHandles()
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

void TcpClient::RequestClose()
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

void TcpClient::FailConnect(const std::string& message)
{
    if (closing)
        return;
    closing = true;
    if (connectPending && !connectResumed)
    {
        connectResumed = true;
        NotifyConnectError(message);
    }
    connectPending = false;
    CloseHandles();
}

void TcpClient::CheckClosed()
{
    if (!closing || closeCount != 0 || addrInited)
        return;
    FinishClosed();
}

void TcpClient::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    if (connected || everConnected)
    {
        if (!disconnectedFired)
        {
            disconnectedFired = true;
            if (cppOnDisconnected)
                cppOnDisconnected();
            else
                disconnectedSig->Fire();
        }
    }
    mgr->RemoveClient(shared_from_this());
    selfGuard.reset();
}

void TcpClient::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<TcpClient*>(handle->data);
    self->closeCount -= 1;
    self->CheckClosed();
}

void TcpClient::OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
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

void TcpClient::OnConnected(uv_connect_t* req, int status)
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
    if (!self->connectResumed)
    {
        self->connectResumed = true;
        self->NotifyConnectOk();
    }
    if (self->closing)
        return;
    if (!self->mgr->shuttingDown)
    {
        if (!self->cppOnConnected)
            self->connectedSig->Fire();
    }
    self->StartReading();
}

void TcpClient::OnConnectTimeout(uv_timer_t* timer)
{
    auto* self = static_cast<TcpClient*>(timer->data);
    if (self->closing)
        return;
    self->FailConnect("connect: connection timed out");
}

void TcpClient::AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle;
    (void)suggestedSize;
    static constexpr size_t kChunkSize = 64 * 1024;
    buf->base = new char[kChunkSize];
    buf->len = kChunkSize;
}

void TcpClient::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<TcpClient*>(stream->data);
    if (nread > 0)
    {
        if (self->cppOnMessage)
        {
            self->cppOnMessage(buf->base, nread);
        }
        else if (!self->mgr->shuttingDown)
        {
            self->messageSig->Fire(Lode::Value(std::string(buf->base, static_cast<size_t>(nread))));
        }
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

void TcpClient::OnWritten(uv_write_t* req, int status)
{
    auto* wreq = reinterpret_cast<WriteRequest*>(req);
    auto* self = static_cast<TcpClient*>(req->handle->data);
    if (status != 0 && !self->closing && !self->closed)
        self->FireError(std::string("write: ") + uv_strerror(status));
    delete wreq;
}

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

} // namespace lodetcp
