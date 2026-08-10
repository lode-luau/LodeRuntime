// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Tcp/TcpServer.hpp"
#include "Tcp/TcpClient.hpp"
#include "Tcp/TcpHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cstring>

namespace lodetcp
{

void TcpServer::InitSignals(Lode::State& vm)
{
    clientSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    clientProxy = clientSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void TcpServer::FireError(const std::string& message)
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

void TcpServer::UpdateAddresses()
{
    struct sockaddr_storage addr;
    int namelen = static_cast<int>(sizeof(addr));
    if (uv_tcp_getsockname(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
    {
        localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
    }
}

Lode::Value TcpServer::MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args)
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

void TcpServer::ListenNative(const std::string& host, int port)
{
    if (closing || closed || listening)
        return;
    std::memset(&tcp, 0, sizeof(tcp));
    tcpInited = true;
    tcp.data = this;
    int r = uv_tcp_init(loop, &tcp);
    if (r != 0)
    {
        BindFailNative(std::string("tcp: ") + uv_strerror(r));
        return;
    }

    struct sockaddr_storage addr;
    int namelen = 0;
    r = MakeSockAddr(host, port, addr, namelen);
    if (r != 0)
    {
        BindFailNative("host must be an IPv4 or IPv6 address");
        return;
    }
    r = uv_tcp_bind(&tcp, reinterpret_cast<const struct sockaddr*>(&addr), 0);
    if (r != 0)
    {
        BindFailNative(std::string("bind: ") + uv_strerror(r));
        return;
    }
    r = uv_listen(reinterpret_cast<uv_stream_t*>(&tcp), backlog, OnConnection);
    if (r != 0)
    {
        BindFailNative(std::string("listen: ") + uv_strerror(r));
        return;
    }
    listening = true;
    UpdateAddresses();
}

Lode::Value TcpServer::MethodLocalAddress(Lode::State& vm)
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

void TcpServer::BindFail(Lode::State& vm, const std::string& message)
{
    if (tcpInited && !tcpClosed)
    {
        tcpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
    }
    vm.RaiseError("socket Server: " + message);
}

void TcpServer::BindFailNative(const std::string& message)
{
    if (tcpInited && !tcpClosed)
    {
        tcpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
    }
    if (cppOnError)
        cppOnError("socket Server: " + message);
}

void TcpServer::RequestClose()
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

void TcpServer::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    listening = false;
    mgr->RemoveServer(shared_from_this());
    selfGuard.reset();
}

int TcpServer::MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
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

void TcpServer::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<TcpServer*>(handle->data);
    self->FinishClosed();
}

void TcpServer::OnConnection(uv_stream_t* server, int status)
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

    if (self->cppOnClient)
    {
        self->cppOnClient(client);
    }
    else
    {
        Lode::Value clientValue = WrapClient(vm, client, self->mgr->clientMethods);
        self->clientSig->Fire(clientValue);
    }
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

} // namespace lodetcp
