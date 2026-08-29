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

Lode::Value UdpSocket::MethodBind(Lode::State& vm, Lode::StackArgs args)
{
    if (closing || closed)
    {
        vm.RaiseError("udp Bind: socket is closed");
        return Lode::Value();
    }
    if (udpInited)
    {
        vm.RaiseError("udp Bind: socket is already bound");
        return Lode::Value();
    }
    if (args.Size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("udp Bind: host must be a string");
        return Lode::Value();
    }
    if (args.Size() < 3 || !args[2].IsNumber())
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

Lode::Value UdpSocket::MethodSend(Lode::State& vm, Lode::StackArgs args)
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
    if (args.Size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
    {
        vm.RaiseError("udp Send: data must be a string or buffer");
        return Lode::Value();
    }
    if (args.Size() < 3 || !args[2].IsString())
    {
        vm.RaiseError("udp Send: host must be a string");
        return Lode::Value();
    }
    if (args.Size() < 4 || !args[3].IsNumber())
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

static bool RequireUdpOptionString(Lode::State& vm, Lode::StackArgs args,
                                   const char* method, std::string& value)
{
    if (args.Size() < 2 || !args[1].IsString())
    {
        vm.RaiseError(std::string("udp ") + method + ": address must be a string");
        return false;
    }
    value = args[1].AsString();
    return true;
}

static bool RequireUdpOptionReady(Lode::State& vm, const UdpSocket& socket, const char* method)
{
    if (!socket.udpInited)
    {
        // Distinguish "never bound" from "closed": the old message claimed
        // the socket was closed when it had simply never been bound.
        vm.RaiseError(std::string("udp ") + method + ": not bound, use Bind first");
        return false;
    }
    if (socket.closed || socket.closing)
    {
        vm.RaiseError(std::string("udp ") + method + ": socket is closed");
        return false;
    }
    return true;
}

Lode::Value UdpSocket::MethodJoinGroup(Lode::State& vm, Lode::StackArgs args)
{
    if (!RequireUdpOptionReady(vm, *this, "JoinGroup")) return Lode::Value();
    std::string group;
    if (!RequireUdpOptionString(vm, args, "JoinGroup", group)) return Lode::Value();
    const char* interfaceName = nullptr;
    std::string interfaceValue;
    if (args.Size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsString())
        {
            vm.RaiseError("udp JoinGroup: interface must be a string");
            return Lode::Value();
        }
        interfaceValue = args[2].AsString();
        interfaceName = interfaceValue.c_str();
    }
    int r = uv_udp_set_membership(&udp, group.c_str(), interfaceName, UV_JOIN_GROUP);
    if (r != 0)
    {
        vm.RaiseError(std::string("udp JoinGroup: ") + uv_strerror(r));
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value UdpSocket::MethodLeaveGroup(Lode::State& vm, Lode::StackArgs args)
{
    if (!RequireUdpOptionReady(vm, *this, "LeaveGroup")) return Lode::Value();
    std::string group;
    if (!RequireUdpOptionString(vm, args, "LeaveGroup", group)) return Lode::Value();
    const char* interfaceName = nullptr;
    std::string interfaceValue;
    if (args.Size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsString())
        {
            vm.RaiseError("udp LeaveGroup: interface must be a string");
            return Lode::Value();
        }
        interfaceValue = args[2].AsString();
        interfaceName = interfaceValue.c_str();
    }
    int r = uv_udp_set_membership(&udp, group.c_str(), interfaceName, UV_LEAVE_GROUP);
    if (r != 0)
    {
        vm.RaiseError(std::string("udp LeaveGroup: ") + uv_strerror(r));
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value UdpSocket::MethodSetBroadcast(Lode::State& vm, Lode::StackArgs args)
{
    if (!RequireUdpOptionReady(vm, *this, "SetBroadcast")) return Lode::Value();
    if (args.Size() < 2 || !args[1].IsBoolean())
    {
        vm.RaiseError("udp SetBroadcast: enabled must be a boolean");
        return Lode::Value();
    }
    int r = uv_udp_set_broadcast(&udp, args[1].AsBoolean() ? 1 : 0);
    if (r != 0) vm.RaiseError(std::string("udp SetBroadcast: ") + uv_strerror(r));
    return Lode::Value();
}

Lode::Value UdpSocket::MethodSetTTL(Lode::State& vm, Lode::StackArgs args)
{
    if (!RequireUdpOptionReady(vm, *this, "SetTTL")) return Lode::Value();
    if (args.Size() < 2 || !args[1].IsNumber() || args[1].AsNumber() < 1 || args[1].AsNumber() > 255)
    {
        vm.RaiseError("udp SetTTL: ttl must be a number from 1 to 255");
        return Lode::Value();
    }
    int r = uv_udp_set_ttl(&udp, static_cast<int>(args[1].AsNumber()));
    if (r != 0) vm.RaiseError(std::string("udp SetTTL: ") + uv_strerror(r));
    return Lode::Value();
}

Lode::Value UdpSocket::MethodSetMulticastLoop(Lode::State& vm, Lode::StackArgs args)
{
    if (!RequireUdpOptionReady(vm, *this, "SetMulticastLoop")) return Lode::Value();
    if (args.Size() < 2 || !args[1].IsBoolean())
    {
        vm.RaiseError("udp SetMulticastLoop: enabled must be a boolean");
        return Lode::Value();
    }
    int r = uv_udp_set_multicast_loop(&udp, args[1].AsBoolean() ? 1 : 0);
    if (r != 0) vm.RaiseError(std::string("udp SetMulticastLoop: ") + uv_strerror(r));
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
            if (flags & UV_UDP_PARTIAL)
            {
                // The datagram did not fit the receive buffer and was cut off.
                // Never present truncated data as a complete datagram.
                self->FireError("read: datagram from " + host + ":" + std::to_string(port) +
                                " was truncated (UV_UDP_PARTIAL) and may be incomplete");
            }
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
    auto* self = static_cast<UdpSocket*>(req && req->handle ? req->handle->data : nullptr);
    if (status != 0 && self && !self->closing && !self->closed)
        self->FireError(std::string("send: ") + uv_strerror(status));
    delete wreq;
}

Lode::Value WrapUdpSocket(Lode::State& vm, const std::shared_ptr<UdpSocket>& socket, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFastFunctionNoYield([socket, methods](Lode::State& vm2, Lode::StackArgs args) -> Lode::Value {
        std::string key = (args.Size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "MessageReceived")
            return socket->messageProxy;
        if (key == "ErrorOccurred")
            return socket->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil())
            return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFastFunctionNoYield([](Lode::State& vm2, Lode::StackArgs) -> Lode::Value {
        vm2.RaiseError("udp: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("UdpSocket")));
    meta.Set("__tostring", vm.CreateFastFunctionNoYield([](Lode::State&, Lode::StackArgs) -> Lode::Value {
        return Lode::Value(std::string("UdpSocket"));
    }));
    Lode::ObjectWrap<UdpSocket>::Wrap(vm, socket, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodeudp
