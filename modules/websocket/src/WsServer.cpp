// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "WebSocket/WsServer.hpp"
#include "WebSocket/WsClient.hpp"
#include "Tcp/TcpServer.hpp"
#include "Tcp/TcpClient.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"

namespace lodews
{

void WsServer::InitSignals(Lode::State& vm)
{
    clientSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    clientProxy = clientSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void WsServer::FireError(const std::string& message)
{
    if (mgr->shuttingDown || closed || closing)
        return;
    errorSig->Fire(Lode::Value(message));
}

void WsServer::UpdateAddresses()
{
    if (tcpServer)
    {
        localHost = tcpServer->localHost;
        localPort = tcpServer->localPort;
    }
}

void WsServer::AttachTcpServer(std::shared_ptr<lodetcp::TcpServer> server)
{
    tcpServer = server;
    tcpServer->cppOnClient = [this](std::shared_ptr<lodetcp::TcpClient> client) {
        OnTcpClient(client);
    };
    tcpServer->cppOnError = [this](const std::string& err) {
        OnTcpError(err);
    };
}

void WsServer::OnTcpClient(std::shared_ptr<lodetcp::TcpClient> tcpClient)
{
    if (closing || closed)
        return;

    auto client = std::make_shared<WsClient>();
    client->mgr = mgr;
    client->mainL = mainL;
    client->loop = loop;
    client->serverSide = true;
    
    Lode::State vm(mainL);
    client->InitSignals(vm);
    client->ownerServer = shared_from_this();
    mgr->AddClient(client);
    client->selfGuard = client;

    client->AttachTcpClient(tcpClient);

    Lode::Value clientValue = WrapClient(vm, client, mgr->clientMethods);
    client->wrappedValue = clientValue;

    // Call OnTcpConnected manually since the connection is already established
    client->OnTcpConnected();
}

void WsServer::OnTcpError(const std::string& err)
{
    if (closed || closing)
        return;
    FireError(err);
}

Lode::Value WsServer::MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closing || closed)
    {
        vm.RaiseError("websocket Server: server is closed");
        return Lode::Value();
    }
    if (listening)
    {
        vm.RaiseError("websocket Server: already listening");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsNumber())
    {
        vm.RaiseError("websocket Server: port must be a number");
        return Lode::Value();
    }
    double portValue = args[1].AsNumber();
    if (!IsValidPort(portValue, true))
    {
        vm.RaiseError("websocket Server: port must be an integer between 0 and 65535");
        return Lode::Value();
    }
    int port = static_cast<int>(portValue);
    std::string host;
    if (args.size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsString())
        {
            vm.RaiseError("websocket Server: host must be a string or nil");
            return Lode::Value();
        }
        host = args[2].AsString();
    }

    struct sockaddr_storage dummyAddr;
    int dummyLen = 0;
    int sockErr = lodetcp::TcpServer::MakeSockAddr(host, port, dummyAddr, dummyLen);
    if (sockErr != 0)
    {
        vm.RaiseError("websocket Server: host must be an IPv4 or IPv6 address");
        return Lode::Value();
    }

    if (!tcpServer)
    {
        tcpServer = std::make_shared<lodetcp::TcpServer>();
        tcpServer->mgr = std::make_shared<lodetcp::TcpManager>();
        tcpServer->mainL = mainL;
        tcpServer->loop = loop;
        tcpServer->InitSignals(vm);
        tcpServer->selfGuard = tcpServer;
    }

    AttachTcpServer(tcpServer);
    tcpServer->ListenNative(host, port);

    listening = true;
    UpdateAddresses();
    return Lode::Value();
}

Lode::Value WsServer::MethodLocalAddress(Lode::State& vm)
{
    if (!listening || closed)
    {
        vm.RaiseError("websocket Server: not listening");
        return Lode::Value();
    }
    Lode::Table t = vm.CreateTable();
    t.Set("host", Lode::Value(localHost));
    t.Set("port", Lode::Value(static_cast<double>(localPort)));
    return Lode::Value(t);
}

void WsServer::RequestClose()
{
    if (closing)
        return;
    closing = true;
    if (tcpServer)
    {
        tcpServer->RequestClose();
        tcpServer.reset();
    }
    CheckClosed();
}

void WsServer::CheckClosed()
{
    if (!closing || closeCount != 0)
        return;
    FinishClosed();
}

void WsServer::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    listening = false;
    mgr->RemoveServer(shared_from_this());
    selfGuard.reset();
}

Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<WsServer>& server, const Lode::Table& methods)
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
        vm2.RaiseError("websocket: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("WebSocketServer")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("WebSocketServer"));
    }));
    Lode::ObjectWrap<WsServer>::Wrap(vm, server, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodews
