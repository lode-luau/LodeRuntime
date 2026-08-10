// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Udp/UdpSocket.hpp"
#include "Udp/UdpHelpers.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cstring>
#include <iostream>

namespace lodeudp
{

struct SendRequest
{
    uv_udp_send_t req;
    std::vector<char> data;
};

void UdpSocket::InitSignals(Lode::State& vm)
{
    messageSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    messageProxy = messageSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void UdpSocket::FireError(const std::string& message)
{
    if (shuttingDown || closed || closing)
        return;
    errorSig->Fire(Lode::Value(message));
}

void UdpSocket::UpdateAddress()
{
    struct sockaddr_storage addr;
    int namelen = static_cast<int>(sizeof(addr));
    if (uv_udp_getsockname(&udp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
    {
        localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
    }
}

Lode::Value UdpSocket::MethodBind(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closing || closed)
    {
        vm.RaiseError("udp Bind: socket is closed");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("udp Bind: host must be a string");
        return Lode::Value();
    }
    if (args.size() < 3 || !args[2].IsNumber())
    {
        vm.RaiseError("udp Bind: port must be a number");
        return Lode::Value();
    }
    
    std::string host = args[1].AsString();
    double portValue = args[2].AsNumber();
    if (!IsValidPort(portValue, true))
    {
        vm.RaiseError("udp Bind: port must be an integer between 0 and 65535");
        return Lode::Value();
    }
    int port = static_cast<int>(portValue);

    std::memset(&udp, 0, sizeof(udp));
    udpInited = true;
    udp.data = this;
    int r = uv_udp_init(loop, &udp);
    if (r != 0)
    {
        BindFail(vm, std::string("udp_init: ") + uv_strerror(r));
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
    
    r = uv_udp_bind(&udp, reinterpret_cast<const struct sockaddr*>(&addr), 0);
    if (r != 0)
    {
        BindFail(vm, std::string("bind: ") + uv_strerror(r));
        return Lode::Value();
    }

    UpdateAddress();
    StartReading();

    return Lode::Value();
}

Lode::Value UdpSocket::MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closing || closed)
    {
        vm.RaiseError("udp Send: socket is closed");
        return Lode::Value();
    }
    if (!udpInited)
    {
        vm.RaiseError("udp Send: not bound, use Bind first");
        return Lode::Value();
    }
    if (args.size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
    {
        vm.RaiseError("udp Send: data must be a string or buffer");
        return Lode::Value();
    }
    if (args.size() < 3 || !args[2].IsString())
    {
        vm.RaiseError("udp Send: host must be a string");
        return Lode::Value();
    }
    if (args.size() < 4 || !args[3].IsNumber())
    {
        vm.RaiseError("udp Send: port must be a number");
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
    
    std::string host = args[2].AsString();
    double portValue = args[3].AsNumber();
    if (!IsValidPort(portValue))
    {
        vm.RaiseError("udp Send: port must be an integer between 1 and 65535");
        return Lode::Value();
    }
    int port = static_cast<int>(portValue);

    struct sockaddr_storage addr;
    int namelen = 0;
    int r = MakeSockAddr(host, port, addr, namelen);
    if (r != 0)
    {
        vm.RaiseError("udp Send: host must be an IPv4 or IPv6 address");
        return Lode::Value();
    }

    auto* wreq = new SendRequest();
    std::memset(&wreq->req, 0, sizeof(wreq->req));
    wreq->req.data = this;
    wreq->data = std::move(data);
    
    uv_buf_t buf;
    buf.base = wreq->data.empty() ? const_cast<char*>("") : wreq->data.data();
    buf.len = wreq->data.size();
    
    r = uv_udp_send(&wreq->req, &udp, &buf, 1, reinterpret_cast<const struct sockaddr*>(&addr), OnSend);
    if (r != 0)
    {
        delete wreq;
        FireError(std::string("send: ") + uv_strerror(r));
        return Lode::Value();
    }
    
    return Lode::Value();
}

Lode::Value UdpSocket::MethodLocalAddress(Lode::State& vm)
{
    if (!udpInited || closed)
    {
        vm.RaiseError("udp LocalAddress: socket not bound");
        return Lode::Value();
    }
    Lode::Table t = vm.CreateTable();
    t.Set("host", Lode::Value(localHost));
    t.Set("port", Lode::Value(static_cast<double>(localPort)));
    return Lode::Value(t);
}

void UdpSocket::BindFail(Lode::State& vm, const std::string& message)
{
    if (udpInited && !udpClosed)
    {
        udpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&udp), OnHandleClosed);
    }
    vm.RaiseError("udp: " + message);
}

void UdpSocket::RequestClose()
{
    if (closing)
        return;
    closing = true;
    if (reading)
    {
        uv_udp_recv_stop(&udp);
        reading = false;
    }
    if (udpInited && !udpClosed)
    {
        udpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&udp), OnHandleClosed);
    }
    else
    {
        FinishClosed();
    }
}

void UdpSocket::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    selfGuard.reset();
}

void UdpSocket::StartReading()
{
    if (reading || closed || closing)
        return;
    reading = true;
    int r = uv_udp_recv_start(&udp, AllocBuffer, OnRead);
    if (r != 0)
    {
        reading = false;
        FireError(std::string("recv_start: ") + uv_strerror(r));
        RequestClose();
    }
}

int UdpSocket::MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
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

void UdpSocket::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<UdpSocket*>(handle->data);
    self->FinishClosed();
}

void UdpSocket::AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle;
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void UdpSocket::OnRead(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)
{
    auto* self = static_cast<UdpSocket*>(handle->data);
    
    if (nread > 0)
    {
        if (!self->shuttingDown && addr)
        {
            Lode::State vm(self->mainL);
            Lode::Table addressTable = vm.CreateTable();
            int port = 0;
            std::string host = FormatSockAddr(addr, port);
            addressTable.Set("host", Lode::Value(host));
            addressTable.Set("port", Lode::Value(static_cast<double>(port)));
            
            self->messageSig->Fire({Lode::Value(std::string(buf->base, static_cast<size_t>(nread))), Lode::Value(addressTable)});
        }
    }
    else if (nread < 0)
    {
        self->FireError(std::string("read: ") + uv_strerror(static_cast<int>(nread)));
    }
    
    if (buf && buf->base)
        delete[] buf->base;
}

void UdpSocket::OnSend(uv_udp_send_t* req, int status)
{
    auto* wreq = reinterpret_cast<SendRequest*>(req);
    auto* self = static_cast<UdpSocket*>(req->handle->data);
    if (status != 0 && !self->closing && !self->closed)
        self->FireError(std::string("send: ") + uv_strerror(status));
    delete wreq;
}

Lode::Value WrapUdpSocket(Lode::State& vm, const std::shared_ptr<UdpSocket>& socket, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([socket, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "MessageReceived")
            return socket->messageProxy;
        if (key == "ErrorOccurred")
            return socket->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("udp: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("UdpSocket")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("UdpSocket"));
    }));
    Lode::ObjectWrap<UdpSocket>::Wrap(vm, socket, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodeudp
