// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Http/HttpServer.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cstring>
#include <iostream>

namespace lodehttp
{

static std::string FormatSockAddr(const struct sockaddr* addr, int& outPort)
{
    char host[NI_MAXHOST];
    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in* a = reinterpret_cast<const struct sockaddr_in*>(addr);
        uv_ip4_name(a, host, sizeof(host));
        outPort = ntohs(a->sin_port);
    }
    else if (addr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6* a = reinterpret_cast<const struct sockaddr_in6*>(addr);
        uv_ip6_name(a, host, sizeof(host));
        outPort = ntohs(a->sin6_port);
    }
    return std::string(host);
}

void HttpServer::InitSignals(Lode::State& vm)
{
    requestSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    requestProxy = requestSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void HttpServer::FireError(const std::string& message)
{
    if (mgr->shuttingDown || closed || closing) return;
    std::vector<Lode::Value> args = { Lode::Value(message) };
    errorSig->Fire(args);
}

void HttpServer::UpdateAddresses()
{
    struct sockaddr_storage addr;
    int namelen = static_cast<int>(sizeof(addr));
    if (uv_tcp_getsockname(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
    {
        localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
    }
}

void HttpServer::BindFail(Lode::State& vm, const std::string& message)
{
    vm.RaiseError("HttpServer Listen: " + message);
}

int HttpServer::MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
{
    std::memset(&out, 0, sizeof(out));
    if (host.empty() || host == "0.0.0.0")
    {
        struct sockaddr_in addr;
        int r = uv_ip4_addr("0.0.0.0", port, &addr);
        if (r == 0)
        {
            std::memcpy(&out, &addr, sizeof(addr));
            outLen = sizeof(addr);
        }
        return r;
    }
    if (host == "::" || host == "::0")
    {
        struct sockaddr_in6 addr;
        int r = uv_ip6_addr("::", port, &addr);
        if (r == 0)
        {
            std::memcpy(&out, &addr, sizeof(addr));
            outLen = sizeof(addr);
        }
        return r;
    }
    struct sockaddr_in addr4;
    int r = uv_ip4_addr(host.c_str(), port, &addr4);
    if (r == 0)
    {
        std::memcpy(&out, &addr4, sizeof(addr4));
        outLen = sizeof(addr4);
        return 0;
    }
    struct sockaddr_in6 addr6;
    r = uv_ip6_addr(host.c_str(), port, &addr6);
    if (r == 0)
    {
        std::memcpy(&out, &addr6, sizeof(addr6));
        outLen = sizeof(addr6);
        return 0;
    }
    return UV_EINVAL;
}

Lode::Value HttpServer::MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (listening) { BindFail(vm, "already listening"); return Lode::Value(); }
    if (closing || closed) { BindFail(vm, "server is closed"); return Lode::Value(); }
    if (args.size() < 2 || !args[1].IsNumber()) { BindFail(vm, "expected port as number"); return Lode::Value(); }

    int port = static_cast<int>(args[1].AsNumber());
    if (args.size() > 2 && !args[2].IsNil() && !args[2].IsString()) {
        BindFail(vm, "expected host as string");
        return Lode::Value();
    }
    std::string host = (args.size() > 2 && args[2].IsString()) ? args[2].AsString() : "0.0.0.0";

    struct sockaddr_storage addr;
    int addrLen = 0;
    int r = MakeSockAddr(host, port, addr, addrLen);
    if (r != 0) { BindFail(vm, uv_strerror(r)); return Lode::Value(); }

    std::memset(&tcp, 0, sizeof(tcp));
    tcpInited = true;
    tcp.data = this;
    r = uv_tcp_init(loop, &tcp);
    if (r != 0) { BindFail(vm, uv_strerror(r)); RequestClose(); return Lode::Value(); }

    r = uv_tcp_bind(&tcp, reinterpret_cast<const struct sockaddr*>(&addr), 0);
    if (r != 0) { BindFail(vm, std::string("bind: ") + uv_strerror(r)); RequestClose(); return Lode::Value(); }

    r = uv_listen(reinterpret_cast<uv_stream_t*>(&tcp), backlog, OnConnection);
    if (r != 0) { BindFail(vm, std::string("listen: ") + uv_strerror(r)); RequestClose(); return Lode::Value(); }

    listening = true;
    UpdateAddresses();
    return Lode::Value();
}

Lode::Value HttpServer::MethodLocalAddress(Lode::State& vm)
{
    if (!listening || closed) { vm.RaiseError("Server LocalAddress: not listening"); return Lode::Value(); }
    Lode::Table t = vm.CreateTable();
    t.Set("host", Lode::Value(localHost));
    t.Set("port", Lode::Value(static_cast<double>(localPort)));
    return Lode::Value(t);
}

void HttpServer::RequestClose()
{
    if (closing) return;
    closing = true;
    if (tcpInited && !tcpClosed)
    {
        tcpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void HttpServer::FinishClosed()
{
    if (closed) return;
    closed = true;
    listening = false;
    mgr->RemoveServer(shared_from_this());
    selfGuard.reset();
}

void HttpServer::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<HttpServer*>(handle->data);
    self->FinishClosed();
}

void HttpServer::OnConnection(uv_stream_t* server, int status)
{
    auto* self = static_cast<HttpServer*>(server->data);
    if (self->closing) return;
    if (status != 0)
    {
        self->FireError(std::string("accept: ") + uv_strerror(status));
        return;
    }
    // Accept connection logic (to be implemented: parse HTTP request and fire RequestReceived)
    // For now we just accept and close immediately.
    uv_tcp_t* client = new uv_tcp_t;
    uv_tcp_init(self->loop, client);
    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(client)) == 0)
    {
        // TODO: Full HTTP server RequestReceived handling goes here
        uv_close(reinterpret_cast<uv_handle_t*>(client), [](uv_handle_t* handle){ delete reinterpret_cast<uv_tcp_t*>(handle); });
    }
    else
    {
        uv_close(reinterpret_cast<uv_handle_t*>(client), [](uv_handle_t* handle){ delete reinterpret_cast<uv_tcp_t*>(handle); });
    }
}

Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<HttpServer>& server, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([server, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "RequestReceived") return server->requestProxy;
        if (key == "ErrorOccurred") return server->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil()) return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("HttpServer: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("HttpServer")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("HttpServer"));
    }));
    Lode::ObjectWrap<HttpServer>::Wrap(vm, server, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<HttpManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Server Listen: invalid Server"); return Lode::Value(); }
        if (mgr->shuttingDown) { vm2.RaiseError("Server Listen: runtime is shutting down"); return Lode::Value(); }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Server Close: invalid Server"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Server Destroy: invalid Server"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsListening", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Server LocalAddress: invalid Server"); return Lode::Value(); }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace lodehttp
