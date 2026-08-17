// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "WebSocket/WsClient.hpp"
#include "WebSocket/WsServer.hpp"
#include "Tcp/TcpClient.hpp"
#include "sha1.hpp"
#include "base64.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Task.hpp"
#include "Lode/ObjectWrap.hpp"
#include <cstring>
#include <algorithm>

namespace lodews
{

void WsClient::InitSignals(Lode::State& vm)
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

void WsClient::FireError(const std::string& message)
{
    if (mgr->shuttingDown || closed || closing)
        return;
    errorSig->Fire(Lode::Value(message));
}

void WsClient::UpdateAddresses()
{
    if (tcpClient)
    {
        localHost = tcpClient->localHost;
        localPort = tcpClient->localPort;
        remoteHost = tcpClient->remoteHost;
        remotePort = tcpClient->remotePort;
    }
}

void WsClient::NotifyConnectOk()
{
    if (!connectCo.IsValid())
        return;
    Lode::State vm(mainL);
    auto res = connectCo.Resume({});
    if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
    connectCo = Lode::Coroutine();
}

void WsClient::NotifyConnectError(const std::string& message)
{
    if (!connectCo.IsValid())
        return;
    Lode::State vm(mainL);
    auto res = connectCo.ResumeError(message);
    if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
    connectCo = Lode::Coroutine();
}

void WsClient::StartTimer(uint64_t ms, TimerMode mode)
{
    if (timerClosed || closing)
        return;
    if (!timerInited)
    {
        std::memset(&timer, 0, sizeof(timer));
        timer.data = this;
        if (uv_timer_init(loop, &timer) != 0)
            return;
        timerInited = true;
    }
    timerMode = mode;
    uv_timer_start(&timer, OnTimer, ms, 0);
}

void WsClient::StopTimer()
{
    if (timerInited && !timerClosed)
        uv_timer_stop(&timer);
}

void WsClient::OnTimer(uv_timer_t* timer)
{
    auto* self = static_cast<WsClient*>(timer->data);
    if (self->closing || self->closed)
        return;
    if (self->timerMode == TimerMode::Connect)
    {
        self->FailConnect("websocket Connect: connection timed out");
    }
    else if (self->timerMode == TimerMode::Close)
    {
        self->RequestClose();
    }
}

void WsClient::FailConnect(const std::string& message)
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

void WsClient::AttachTcpClient(std::shared_ptr<lodetcp::TcpClient> client)
{
    tcpClient = client;
    
    tcpClient->cppOnConnected = [this]() {
        OnTcpConnected();
    };
    tcpClient->cppOnError = [this](const std::string& err) {
        OnTcpError(err);
    };
    tcpClient->cppOnMessage = [this](const char* data, size_t size) {
        OnTcpMessage(data, size);
    };
    tcpClient->cppOnDisconnected = [this]() {
        OnTcpDisconnected();
    };
}

void WsClient::OnTcpConnected()
{
    if (closing || closed)
        return;
    UpdateAddresses();
    if (!serverSide)
    {
#ifdef _WIN32
        if (parsedUrl.isSecure && tls)
        {
            tlsHandshaking = true;
            auto clientHello = tls->StartHandshake(parsedUrl.host);
            SendRawTls(clientHello);
            return;
        }
#endif

        uint8_t nonce[16];
        mgr->rng.Fill(nonce, sizeof(nonce));
        sentKey = ws::Base64Encode(nonce, sizeof(nonce));

        std::string hostHeader = parsedUrl.host;
        if (parsedUrl.port != (parsedUrl.isSecure ? 443 : 80))
            hostHeader += ":" + std::to_string(parsedUrl.port);

        std::string request = "GET " + parsedUrl.path + " HTTP/1.1\r\n"
                              "Host: " + hostHeader + "\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: " + sentKey + "\r\n"
                              "Sec-WebSocket-Version: 13\r\n";
        for (const auto& kv : requestHeaders)
            request += kv.first + ": " + kv.second + "\r\n";
        request += "\r\n";
        
        SendRaw(std::vector<char>(request.begin(), request.end()));
    }
}

void WsClient::OnTcpError(const std::string& err)
{
    if (connectPending)
    {
        FailConnect("websocket Connect: " + err);
    }
    else
    {
        if (!closed && !closing)
            FireError(err);
        RequestClose();
    }
}

void WsClient::OnTcpMessage(const char* data, size_t size)
{
    if (closing || closed)
        return;

#ifdef _WIN32
    if (parsedUrl.isSecure && tls)
    {
        if (tlsHandshaking)
        {
            std::vector<uint8_t> outSend;
            std::string errOut;
            auto result = tls->ProcessHandshakeData(
                reinterpret_cast<const uint8_t*>(data), size, outSend, errOut);

            switch (result)
            {
            case lodehttp::TlsContext::HandshakeResult::NeedMoreData:
                return;

            case lodehttp::TlsContext::HandshakeResult::DataToSend:
                SendRawTls(outSend);
                return;

            case lodehttp::TlsContext::HandshakeResult::Complete:
            {
                tlsHandshaking = false;
                tls->DrainPending();

                uint8_t nonce[16];
                mgr->rng.Fill(nonce, sizeof(nonce));
                sentKey = ws::Base64Encode(nonce, sizeof(nonce));

                std::string hostHeader = parsedUrl.host;
                if (parsedUrl.port != 443)
                    hostHeader += ":" + std::to_string(parsedUrl.port);

                std::string request = "GET " + parsedUrl.path + " HTTP/1.1\r\n"
                                      "Host: " + hostHeader + "\r\n"
                                      "Upgrade: websocket\r\n"
                                      "Connection: Upgrade\r\n"
                                      "Sec-WebSocket-Key: " + sentKey + "\r\n"
                                      "Sec-WebSocket-Version: 13\r\n";
                for (const auto& kv : requestHeaders)
                    request += kv.first + ": " + kv.second + "\r\n";
                request += "\r\n";

                SendRaw(std::vector<char>(request.begin(), request.end()));
                return;
            }

            case lodehttp::TlsContext::HandshakeResult::Error:
                OnTcpError("tls: " + errOut);
                return;
            }
        }
        else
        {
            std::string decErr;
            auto decrypted = tls->Decrypt(
                reinterpret_cast<const uint8_t*>(data), size, decErr);
            if (!decErr.empty())
            {
                OnTcpError("tls: " + decErr);
                return;
            }
            if (!decrypted.empty())
            {
                recvBuf.insert(recvBuf.end(), decrypted.begin(), decrypted.end());
                ProcessData();
            }
            return;
        }
    }
#endif

    recvBuf.insert(recvBuf.end(), data, data + size);
    ProcessData();
}

void WsClient::OnTcpDisconnected()
{
    if (handshakeComplete && !closing && !closed)
    {
        if (!hasCloseInfo)
        {
            hasCloseInfo = true;
            closeCode = kCloseAbnormal;
            closeReason = "";
        }
        RequestClose();
    }
    else
    {
        RequestClose();
    }
}

void WsClient::ProcessData()
{
    if (!handshakeComplete)
    {
        static const std::string kTerminator = "\r\n\r\n";
        auto it = std::search(recvBuf.begin(), recvBuf.end(), kTerminator.begin(), kTerminator.end());
        if (it == recvBuf.end())
            return;
        size_t headerLen = static_cast<size_t>(it - recvBuf.begin()) + kTerminator.size();
        std::string headerBlock(recvBuf.begin(), recvBuf.begin() + headerLen);
        recvBuf.erase(recvBuf.begin(), recvBuf.begin() + headerLen);
        
        if (serverSide)
            DoServerHandshake(headerBlock);
        else
            DoClientHandshake(headerBlock);
            
        if (closed || closing)
            return;
    }
    ParseFrames();
}

void WsClient::DoServerHandshake(const std::string& headerBlock)
{
    ParsedHeaders headers;
    if (!ParseHeaderBlock(headerBlock, headers))
    {
        if (!closed && !closing)
            FireError("malformed handshake");
        RequestClose();
        return;
    }

    if (headers.statusLine.compare(0, 4, "GET ") != 0 ||
        headers.statusLine.find(" HTTP/1.1") == std::string::npos)
    {
        std::string resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        SendRaw(std::vector<char>(resp.begin(), resp.end()));
        if (!closed && !closing)
            FireError("invalid websocket handshake request");
        RequestClose();
        return;
    }
    const std::string* upgrade = headers.Find("Upgrade");
    const std::string* connection = headers.Find("Connection");
    const std::string* key = headers.Find("Sec-WebSocket-Key");
    const std::string* version = headers.Find("Sec-WebSocket-Version");
    if (!upgrade || !ParsedHeaders::EqualsIgnoreCase(*upgrade, "websocket") ||
        !HeaderListContains(connection, "Upgrade") || !key || !version || *version != "13")
    {
        std::string resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        SendRaw(std::vector<char>(resp.begin(), resp.end()));
        if (!closed && !closing)
            FireError("invalid websocket handshake request");
        RequestClose();
        return;
    }
    std::string accept = ComputeAcceptKey(*key);
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    SendRaw(std::vector<char>(response.begin(), response.end()));
    
    handshakeComplete = true;
    StopTimer();
    opened = true;
    state = WsState::Open;
    if (!mgr->shuttingDown && wrappedValue.IsUserdata())
    {
        ownerServer->clientSig->Fire(wrappedValue);
    }
}

void WsClient::DoClientHandshake(const std::string& headerBlock)
{
    ParsedHeaders headers;
    if (!ParseHeaderBlock(headerBlock, headers))
    {
        FailConnect("malformed handshake");
        return;
    }
    if (headers.statusLine.find(" 101 ") == std::string::npos)
    {
        FailConnect("websocket handshake rejected (expected 101)");
        return;
    }
    const std::string* upgrade = headers.Find("Upgrade");
    const std::string* connection = headers.Find("Connection");
    const std::string* accept = headers.Find("Sec-WebSocket-Accept");
    if (!upgrade || !ParsedHeaders::EqualsIgnoreCase(*upgrade, "websocket") ||
        !HeaderListContains(connection, "Upgrade") || !accept || *accept != ComputeAcceptKey(sentKey))
    {
        FailConnect("websocket handshake rejected (bad accept key)");
        return;
    }
    
    handshakeComplete = true;
    StopTimer();
    opened = true;
    state = WsState::Open;
    if (!mgr->shuttingDown)
        connectedSig->Fire();
    if (!connectResumed && connectCo.IsValid())
    {
        connectResumed = true;
        NotifyConnectOk();
    }
}

void WsClient::ParseFrames()
{
    while (recvBuf.size() >= 2)
    {
        uint8_t b0 = static_cast<uint8_t>(recvBuf[0]);
        uint8_t b1 = static_cast<uint8_t>(recvBuf[1]);

        bool fin = (b0 & 0x80) != 0;
        uint8_t rsv = b0 & 0x70;
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t payloadLen = b1 & 0x7F;

        if (rsv != 0)
        {
            FireError("websocket: protocol error: reserved bits set");
            if (!closeSent)
            {
                closeSent = true;
                SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, ""));
            }
            RequestClose();
            return;
        }

        size_t headerLen = 2;
        if (payloadLen == 126)
        {
            if (recvBuf.size() < 4)
                return;
            payloadLen = (static_cast<uint64_t>(static_cast<uint8_t>(recvBuf[2])) << 8)
                       | static_cast<uint8_t>(recvBuf[3]);
            headerLen = 4;
        }
        else if (payloadLen == 127)
        {
            if (recvBuf.size() < 10)
                return;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | static_cast<uint8_t>(recvBuf[2 + i]);
            headerLen = 10;
        }

        uint32_t maskKey = 0;
        if (masked)
        {
            if (recvBuf.size() < headerLen + 4)
                return;
            maskKey = (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[headerLen])) << 0)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[headerLen + 1])) << 8)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[headerLen + 2])) << 16)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[headerLen + 3])) << 24);
            headerLen += 4;
        }

        size_t total = headerLen + static_cast<size_t>(payloadLen);
        if (recvBuf.size() < total)
            return;

        std::vector<char> payload(recvBuf.begin() + headerLen, recvBuf.begin() + total);
        recvBuf.erase(recvBuf.begin(), recvBuf.begin() + total);

        if (masked)
        {
            if (!serverSide)
            {
                FireError("websocket: protocol error: server frame was masked");
                if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
                RequestClose();
                return;
            }
            for (size_t i = 0; i < payload.size(); ++i)
            {
                uint8_t keyByte = static_cast<uint8_t>((maskKey >> (8 * (i % 4))) & 0xFF);
                payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ keyByte);
            }
        }
        else if (serverSide)
        {
            FireError("websocket: protocol error: client frame was not masked");
            if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
            RequestClose();
            return;
        }

        if (opcode == kOpcodePing || opcode == kOpcodePong || opcode == kOpcodeClose)
        {
            if (!fin || payload.size() > 125)
            {
                FireError("websocket: protocol error: invalid control frame");
                if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
                RequestClose();
                return;
            }
            if (opcode == kOpcodePing)
            {
                SendFrame(kOpcodePong, payload);
                continue;
            }
            if (opcode == kOpcodePong)
                continue;
            
            // Close frame
            hasCloseInfo = true;
            closeCode = kCloseAbnormal;
            if (payload.size() >= 2)
            {
                closeCode = (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]);
                if (payload.size() > 2)
                    closeReason.assign(payload.begin() + 2, payload.end());
            }
            closeReceived = true;
            if (!closeSent)
            {
                closeSent = true;
                SendFrame(kOpcodeClose, MakeClosePayload(static_cast<uint16_t>(closeCode == kCloseAbnormal ? kCloseNormal : closeCode), ""));
            }
            if (state == WsState::Open)
                state = WsState::Closing;
            RequestClose();
            return;
        }

        if (opcode == kOpcodeContinuation)
        {
            if (!fragmentInProgress)
            {
                FireError("websocket: protocol error: unexpected continuation frame");
                if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
                RequestClose();
                return;
            }
            fragmentBuf.insert(fragmentBuf.end(), payload.begin(), payload.end());
            if (fin)
            {
                std::vector<char> message = std::move(fragmentBuf);
                uint8_t messageOpcode = fragmentOpcode;
                fragmentBuf.clear();
                fragmentInProgress = false;
                fragmentOpcode = 0;
                
                if (!mgr->shuttingDown)
                {
                    std::string text(message.begin(), message.end());
                    messageSig->Fire(Lode::Value(text));
                }
            }
            continue;
        }

        if (opcode != kOpcodeText && opcode != kOpcodeBinary)
        {
            FireError("websocket: protocol error: unknown opcode");
            if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
            RequestClose();
            return;
        }
        if (fragmentInProgress)
        {
            FireError("websocket: protocol error: new data frame while fragmented");
            if (!closeSent) { closeSent = true; SendFrame(kOpcodeClose, MakeClosePayload(kCloseProtocolError, "")); }
            RequestClose();
            return;
        }
        if (!fin)
        {
            fragmentOpcode = opcode;
            fragmentInProgress = true;
            fragmentBuf = std::move(payload);
            continue;
        }
        
        if (!mgr->shuttingDown)
        {
            std::string text(payload.begin(), payload.end());
            messageSig->Fire(Lode::Value(text));
        }
    }
}

Lode::Value WsClient::MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (opened || state == WsState::Closing || state == WsState::Closed)
    {
        vm.RaiseError("websocket Connect: already connected or closed");
        return Lode::Value();
    }
    if (connectPending)
    {
        vm.RaiseError("websocket Connect: a connection attempt is already in progress");
        return Lode::Value();
    }
    if (closing || closed)
    {
        vm.RaiseError("websocket Connect: socket is closed");
        return Lode::Value();
    }
    if (args.size() < 2 || !args[1].IsString())
    {
        vm.RaiseError("websocket Connect: url must be a string");
        return Lode::Value();
    }
    std::string url = args[1].AsString();
    if (url.empty())
    {
        vm.RaiseError("websocket Connect: url must not be empty");
        return Lode::Value();
    }

    ParsedWsUrl parsed = ParseWebSocketUrl(url);
    if (!parsed.valid)
    {
        vm.RaiseError("websocket Connect: invalid-url: " + parsed.error);
        return Lode::Value();
    }

    if (parsed.isSecure)
    {
#ifdef _WIN32
        tls = std::make_unique<lodehttp::TlsContext>();
        if (!tls->Init())
        {
            vm.RaiseError("websocket Connect: tls: failed to acquire SSPI credentials");
            return Lode::Value();
        }
#else
        vm.RaiseError("websocket Connect: tls: WSS is not supported on this platform");
        return Lode::Value();
#endif
    }

    uint64_t timeoutMs = 0;
    if (args.size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsTable())
        {
            vm.RaiseError("websocket Connect: opts must be a table or nil");
            return Lode::Value();
        }
        auto opts = args[2].AsTable();
        auto timeout = opts.Get("timeout");
        if (timeout.IsOk() && !timeout.GetValue().IsNil())
        {
            if (!timeout.GetValue().IsNumber())
            {
                vm.RaiseError("websocket Connect: timeout must be a number or nil");
                return Lode::Value();
            }
            auto ms = Lode::Numeric::ToMilliseconds(timeout.GetValue().AsNumber(), 1000.0, "timeout");
            if (ms.IsError())
            {
                vm.RaiseError(ms.GetError().ErrorMessage());
                return Lode::Value();
            }
            timeoutMs = ms.GetValue();
        }
        auto headers = opts.Get("headers");
        if (headers.IsOk() && !headers.GetValue().IsNil())
        {
            if (!headers.GetValue().IsTable())
            {
                vm.RaiseError("websocket Connect: opts.headers must be a table or nil");
                return Lode::Value();
            }
            auto headerTable = headers.GetValue().AsTable();
            auto keys = headerTable.GetKeys();
            for (const auto& key : keys)
            {
                auto val = headerTable.Get(key);
                if (!val.IsOk() || !val.GetValue().IsString())
                {
                    vm.RaiseError("websocket Connect: opts.headers values must be strings");
                    return Lode::Value();
                }
                requestHeaders.emplace_back(key, val.GetValue().AsString());
            }
        }
    }

    parsedUrl = parsed;
    connectTimeoutMs = timeoutMs;
    connectPending = true;
    connectCo = Lode::Coroutine(vm.GetLuaState());

    if (connectTimeoutMs > 0)
        StartTimer(connectTimeoutMs, TimerMode::Connect);

    tcpClient = std::make_shared<lodetcp::TcpClient>();
    tcpClient->mgr = std::make_shared<lodetcp::TcpManager>();
    tcpClient->mainL = mainL;
    tcpClient->loop = loop;
    tcpClient->InitSignals(vm);
    tcpClient->selfGuard = tcpClient;
    AttachTcpClient(tcpClient);
    int r = tcpClient->ConnectNative(parsedUrl.connectHost, parsedUrl.port, connectTimeoutMs);
    if (r != 0)
    {
        connectPending = false;
        connectCo = Lode::Coroutine();
        vm.RaiseError("websocket Connect: " + std::string(uv_strerror(r)));
        return Lode::Value();
    }

    return vm.YieldThread();
}

void WsClient::SendRaw(const std::vector<char>& data)
{
    if (!tcpClient || data.empty())
        return;

#ifdef _WIN32
    if (parsedUrl.isSecure && tls && !tlsHandshaking)
    {
        std::string encErr;
        auto enc = tls->Encrypt(data.data(), data.size(), encErr);
        if (!encErr.empty() || enc.empty())
        {
            FireError("tls: " + encErr);
            return;
        }
        tcpClient->SendNative(reinterpret_cast<const char*>(enc.data()), enc.size());
        return;
    }
#endif

    tcpClient->SendNative(data.data(), data.size());
}

void WsClient::SendRawTls(const std::vector<uint8_t>& data)
{
    if (tcpClient && !data.empty())
        tcpClient->SendNative(reinterpret_cast<const char*>(data.data()), data.size());
}

void WsClient::SendFrame(uint8_t opcode, const std::vector<char>& payload)
{
    uint32_t maskKey = serverSide ? 0u : mgr->rng.Next();
    std::vector<char> frame = EncodeFrame(opcode, payload, true, !serverSide, maskKey);
    SendRaw(frame);
}

Lode::Value WsClient::MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("websocket Send: socket is closed");
        return Lode::Value();
    }
    if (state != WsState::Open)
    {
        vm.RaiseError("websocket Send: not connected");
        return Lode::Value();
    }
    if (args.size() < 2 || (!args[1].IsString() && !args[1].IsBuffer()))
    {
        vm.RaiseError("websocket Send: data must be a string or buffer");
        return Lode::Value();
    }
    std::vector<char> data;
    uint8_t opcode = kOpcodeText;
    if (args[1].IsString())
    {
        const std::string& text = args[1].AsString();
        data.assign(text.begin(), text.end());
    }
    else
    {
        size_t size = 0;
        void* ptr = args[1].AsBuffer(&size);
        opcode = kOpcodeBinary;
        if (ptr && size > 0)
            data.assign(static_cast<const char*>(ptr), static_cast<const char*>(ptr) + size);
    }
    SendFrame(opcode, data);
    return Lode::Value();
}

Lode::Value WsClient::MethodPing(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing)
    {
        vm.RaiseError("websocket Ping: socket is closed");
        return Lode::Value();
    }
    if (state != WsState::Open)
    {
        vm.RaiseError("websocket Ping: not connected");
        return Lode::Value();
    }
    if (args.size() > 1 && !args[1].IsNil() && !args[1].IsString() && !args[1].IsBuffer())
    {
        vm.RaiseError("websocket Ping: data must be a string or buffer or nil");
        return Lode::Value();
    }
    std::vector<char> data;
    if (args.size() > 1 && !args[1].IsNil())
    {
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
    }
    if (data.size() > 125)
    {
        vm.RaiseError("websocket Ping: payload must be at most 125 bytes");
        return Lode::Value();
    }
    SendFrame(kOpcodePing, data);
    return Lode::Value();
}

Lode::Value WsClient::MethodClose(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed || closing || state == WsState::Closed)
    {
        vm.RaiseError("websocket Close: socket is already closing or closed");
        return Lode::Value();
    }
    uint16_t code = kCloseNormal;
    std::string reason;
    if (args.size() > 1 && !args[1].IsNil())
    {
        if (!args[1].IsNumber())
        {
            vm.RaiseError("websocket Close: code must be a number or nil");
            return Lode::Value();
        }
        if (!IsValidCloseCode(args[1].AsNumber()))
        {
            vm.RaiseError("websocket Close: invalid close code");
            return Lode::Value();
        }
        code = static_cast<uint16_t>(args[1].AsNumber());
    }
    if (args.size() > 2 && !args[2].IsNil())
    {
        if (!args[2].IsString())
        {
            vm.RaiseError("websocket Close: reason must be a string or nil");
            return Lode::Value();
        }
        reason = args[2].AsString();
        if (reason.size() > 123)
        {
            vm.RaiseError("websocket Close: reason must be at most 123 bytes");
            return Lode::Value();
        }
    }

    if (state == WsState::Open)
    {
        closeSent = true;
        SendFrame(kOpcodeClose, MakeClosePayload(code, reason));
        state = WsState::Closing;
        StartTimer(5000, TimerMode::Close);
    }
    else if (state == WsState::Connecting)
    {
        RequestClose();
    }
    return Lode::Value();
}

Lode::Value WsClient::MethodLocalAddress(Lode::State& vm)
{
    if (state == WsState::Connecting || closed)
    {
        vm.RaiseError("websocket LocalAddress: not connected");
        return Lode::Value();
    }
    Lode::Table t = vm.CreateTable();
    t.Set("host", Lode::Value(localHost));
    t.Set("port", Lode::Value(static_cast<double>(localPort)));
    return Lode::Value(t);
}

Lode::Value WsClient::MethodRemoteAddress(Lode::State& vm)
{
    if (state == WsState::Connecting || closed)
    {
        vm.RaiseError("websocket RemoteAddress: not connected");
        return Lode::Value();
    }
    Lode::Table t = vm.CreateTable();
    t.Set("host", Lode::Value(remoteHost));
    t.Set("port", Lode::Value(static_cast<double>(remotePort)));
    return Lode::Value(t);
}

void WsClient::CloseHandles()
{
    if (closed)
        return;
    connectPending = false;
    StopTimer();
    if (timerInited && !timerClosed)
    {
        timerClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&timer), OnHandleClosed);
        ++closeCount;
    }
    if (tcpClient)
    {
        tcpClient->RequestClose();
        tcpClient.reset();
    }
    CheckClosed();
}

void WsClient::RequestClose(uint16_t code, const std::string& reason)
{
    (void)code;
    (void)reason;
    if (closing)
        return;
    closing = true;
    if (connectPending && !connectResumed && connectCo.IsValid())
    {
        connectResumed = true;
        NotifyConnectError("websocket Connect: connection closed");
    }
    connectPending = false;
    CloseHandles();
}

void WsClient::CheckClosed()
{
    if (!closing || closeCount != 0)
        return;
    FinishClosed();
}

void WsClient::FinishClosed()
{
    if (closed)
        return;
    closed = true;
    state = WsState::Closed;
    if (opened && !disconnectedFired && !mgr->shuttingDown)
    {
        disconnectedFired = true;
        if (hasCloseInfo)
            disconnectedSig->Fire({Lode::Value(static_cast<double>(closeCode)), Lode::Value(closeReason)});
        else
            disconnectedSig->Fire({Lode::Value(static_cast<double>(kCloseAbnormal)), Lode::Value(std::string(""))});
    }
    mgr->RemoveClient(shared_from_this());
    selfGuard.reset();
}

void WsClient::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<WsClient*>(handle->data);
    self->closeCount -= 1;
    self->CheckClosed();
}

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<WsClient>& client, const Lode::Table& methods)
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
        vm2.RaiseError("websocket: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("WebSocket")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("WebSocket"));
    }));
    Lode::ObjectWrap<WsClient>::Wrap(vm, client, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

} // namespace lodews
