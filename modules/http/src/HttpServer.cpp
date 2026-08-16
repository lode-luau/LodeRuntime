// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Http/HttpServer.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Task.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace lodehttp
{

namespace
{

std::string StatusReason(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

std::string ToLower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string FormatSockAddr(const struct sockaddr* addr, int& outPort)
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

struct WriteReq
{
    uv_write_t req;
    std::string data;
    HttpServerConnection* conn = nullptr;
};

} // namespace

// =======================================================
// HttpServerResponse Implementation
// =======================================================

void HttpServerResponse::SetStatus(int code, const std::string& text)
{
    statusCode = code;
    statusText = text.empty() ? StatusReason(code) : text;
}

void HttpServerResponse::SetHeader(const std::string& name, const std::string& value)
{
    std::string lower = ToLower(name);
    for (auto& h : headers)
    {
        if (ToLower(h.first) == lower)
        {
            h.second = value;
            return;
        }
    }
    headers.push_back({ name, value });
}

void HttpServerResponse::Write(const char* data, size_t len)
{
    if (finished || !data || len == 0) return;
    bodyBuffer.append(data, len);
}

void HttpServerResponse::End(const char* data, size_t len)
{
    if (finished) return;
    finished = true;
    if (data && len > 0)
        bodyBuffer.append(data, len);

    bool hasContentLength = false;
    bool hasConnection = false;

    for (const auto& h : headers)
    {
        std::string l = ToLower(h.first);
        if (l == "content-length") hasContentLength = true;
        if (l == "connection") hasConnection = true;
    }

    if (!hasContentLength)
        SetHeader("Content-Length", std::to_string(bodyBuffer.size()));
    if (!hasConnection)
        SetHeader("Connection", "close");

    std::string raw = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
    for (const auto& h : headers)
    {
        raw += h.first + ": " + h.second + "\r\n";
    }
    raw += "\r\n";
    raw += bodyBuffer;

    auto conn = weakConn.lock();
    if (conn)
    {
        conn->SendRawResponse(raw);
    }
}

// =======================================================
// HttpServerConnection Implementation
// =======================================================

void HttpServerConnection::StartReading()
{
    if (clientClosed || !clientInited) return;
    uv_read_start(reinterpret_cast<uv_stream_t*>(&client), AllocBuffer, OnRead);
}

void HttpServerConnection::StopReading()
{
    if (clientClosed || !clientInited) return;
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&client));
}

void HttpServerConnection::AllocBuffer(uv_handle_t*, size_t suggestedSize, uv_buf_t* buf)
{
    buf->base = new char[suggestedSize];
    buf->len = suggestedSize;
}

void HttpServerConnection::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<HttpServerConnection*>(stream->data);
    if (!self || self->clientClosed)
    {
        if (buf->base) delete[] buf->base;
        return;
    }

    if (nread > 0)
    {
        self->OnData(buf->base, static_cast<size_t>(nread));
    }
    else if (nread < 0)
    {
        if (nread == UV_EOF)
        {
            if (self->headersParsed && (!self->hasContentLength || self->body.size() >= self->expectedContentLength))
            {
                self->DispatchRequest();
            }
        }
        self->Close();
    }

    if (buf->base) delete[] buf->base;
}

void HttpServerConnection::OnData(const char* data, size_t len)
{
    rawBuffer.append(data, len);

    if (TryParseRequest())
    {
        StopReading();
        DispatchRequest();
    }
}

bool HttpServerConnection::TryParseRequest()
{
    if (!headersParsed)
    {
        size_t idx = rawBuffer.find("\r\n\r\n");
        size_t delimLen = 4;
        if (idx == std::string::npos)
        {
            idx = rawBuffer.find("\n\n");
            delimLen = 2;
        }

        if (idx == std::string::npos)
            return false;

        headerEndPos = idx + delimLen;
        std::string headerStr = rawBuffer.substr(0, idx);

        size_t lineEnd = headerStr.find("\r\n");
        size_t lineDelim = 2;
        if (lineEnd == std::string::npos)
        {
            lineEnd = headerStr.find("\n");
            lineDelim = 1;
        }
        if (lineEnd == std::string::npos)
            return false;

        std::string reqLine = headerStr.substr(0, lineEnd);
        size_t sp1 = reqLine.find(' ');
        if (sp1 == std::string::npos) return false;
        size_t sp2 = reqLine.find(' ', sp1 + 1);

        method = reqLine.substr(0, sp1);
        if (sp2 == std::string::npos)
        {
            url = reqLine.substr(sp1 + 1);
            httpVersion = "HTTP/1.1";
        }
        else
        {
            url = reqLine.substr(sp1 + 1, sp2 - (sp1 + 1));
            httpVersion = reqLine.substr(sp2 + 1);
        }

        size_t qPos = url.find('?');
        if (qPos != std::string::npos)
        {
            path = url.substr(0, qPos);
            query = url.substr(qPos + 1);
        }
        else
        {
            path = url;
            query = "";
        }

        size_t pos = lineEnd + lineDelim;
        while (pos < headerStr.size())
        {
            size_t nextEnd = headerStr.find("\r\n", pos);
            size_t nextDelim = 2;
            if (nextEnd == std::string::npos)
            {
                nextEnd = headerStr.find("\n", pos);
                nextDelim = 1;
            }
            std::string line = (nextEnd == std::string::npos) ? headerStr.substr(pos) : headerStr.substr(pos, nextEnd - pos);
            if (!line.empty())
            {
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string hName = line.substr(0, colon);
                    std::string hVal = line.substr(colon + 1);
                    while (!hVal.empty() && (hVal.front() == ' ' || hVal.front() == '\t')) hVal.erase(0, 1);
                    while (!hVal.empty() && (hVal.back() == ' ' || hVal.back() == '\t')) hVal.pop_back();

                    requestHeaders.push_back({ hName, hVal });

                    if (ToLower(hName) == "content-length")
                    {
                        try {
                            expectedContentLength = std::stoull(hVal);
                            hasContentLength = true;
                        } catch (...) {}
                    }
                }
            }
            if (nextEnd == std::string::npos) break;
            pos = nextEnd + nextDelim;
        }

        headersParsed = true;
    }

    if (headersParsed)
    {
        size_t bodyBytesAvailable = rawBuffer.size() - headerEndPos;
        if (hasContentLength)
        {
            if (bodyBytesAvailable >= expectedContentLength)
            {
                body = rawBuffer.substr(headerEndPos, expectedContentLength);
                return true;
            }
            return false;
        }
        else
        {
            body = rawBuffer.substr(headerEndPos);
            return true;
        }
    }

    return false;
}

void HttpServerConnection::DispatchRequest()
{
    if (!server || !server->mainL || server->closed || server->closing)
        return;

    Lode::State vm(server->mainL);

    Lode::Table reqTable = vm.CreateTable();
    reqTable.Set("method", Lode::Value(method));
    reqTable.Set("url", Lode::Value(url));
    reqTable.Set("path", Lode::Value(path));
    reqTable.Set("query", Lode::Value(query));
    reqTable.Set("version", Lode::Value(httpVersion));
    reqTable.Set("body", Lode::Value(body));

    Lode::Table headersTable = vm.CreateTable();
    for (const auto& h : requestHeaders)
    {
        headersTable.Set(h.first, Lode::Value(h.second));
        headersTable.Set(ToLower(h.first), Lode::Value(h.second));
    }
    reqTable.Set("headers", Lode::Value(headersTable));

    auto res = std::make_shared<HttpServerResponse>();
    res->weakConn = shared_from_this();

    Lode::Value resVal = WrapServerResponse(vm, res, server->responseMethods);

    server->requestSig->Fire({ Lode::Value(reqTable), resVal });
}

void HttpServerConnection::SendRawResponse(const std::string& raw)
{
    if (clientClosed || !clientInited) return;

    auto* wr = new WriteReq();
    std::memset(&wr->req, 0, sizeof(wr->req));
    wr->req.data = wr;
    wr->conn = this;
    wr->data = raw;

    uv_buf_t buf;
    buf.base = wr->data.data();
    buf.len = wr->data.size();

    int r = uv_write(&wr->req, reinterpret_cast<uv_stream_t*>(&client), &buf, 1, [](uv_write_t* req, int) {
        auto* wr = static_cast<WriteReq*>(req->data);
        if (wr)
        {
            if (wr->conn)
                wr->conn->Close();
            delete wr;
        }
    });

    if (r != 0)
    {
        delete wr;
        Close();
    }
}

void HttpServerConnection::Close()
{
    if (clientClosed) return;
    clientClosed = true;
    StopReading();
    if (clientInited)
    {
        uv_close(reinterpret_cast<uv_handle_t*>(&client), OnClose);
    }
}

void HttpServerConnection::OnClose(uv_handle_t* handle)
{
    auto* self = static_cast<HttpServerConnection*>(handle->data);
    if (self)
    {
        if (self->server)
            self->server->RemoveConnection(self->shared_from_this());
        self->selfGuard.reset();
    }
}

// =======================================================
// HttpServer Implementation
// =======================================================

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

void HttpServer::AddConnection(const std::shared_ptr<HttpServerConnection>& conn)
{
    connections.push_back(conn);
}

void HttpServer::RemoveConnection(const std::shared_ptr<HttpServerConnection>& conn)
{
    auto it = std::find(connections.begin(), connections.end(), conn);
    if (it != connections.end())
        connections.erase(it);
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
    listening = false;

    auto conns = connections;
    for (auto& c : conns)
    {
        if (c) c->Close();
    }
    connections.clear();

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
    if (!self || self->closing || self->closed) return;
    if (status != 0)
    {
        self->FireError(std::string("accept: ") + uv_strerror(status));
        return;
    }

    auto conn = std::make_shared<HttpServerConnection>();
    conn->server = self;
    conn->clientInited = true;
    conn->client.data = conn.get();

    uv_tcp_init(self->loop, &conn->client);
    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&conn->client)) == 0)
    {
        self->AddConnection(conn);
        conn->selfGuard = conn;
        conn->StartReading();
    }
    else
    {
        conn->Close();
    }
}

// =======================================================
// Metatables and Bindings
// =======================================================

Lode::Value WrapServerResponse(Lode::State& vm, const std::shared_ptr<HttpServerResponse>& res, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([res, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "status") return Lode::Value(static_cast<double>(res->statusCode));
        if (key == "statusText") return Lode::Value(res->statusText);
        if (key == "headersSent") return Lode::Value(res->finished);

        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil()) return value.GetValue();

        if (!key.empty())
        {
            std::string altKey = key;
            if (altKey[0] >= 'a' && altKey[0] <= 'z') altKey[0] = static_cast<char>(altKey[0] - 'a' + 'A');
            else if (altKey[0] >= 'A' && altKey[0] <= 'Z') altKey[0] = static_cast<char>(altKey[0] - 'A' + 'a');
            auto altVal = methods.Get(altKey);
            if (altVal.IsOk() && !altVal.GetValue().IsNil()) return altVal.GetValue();
        }

        return Lode::Value();
    }));

    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("ServerResponse: objects are read-only");
        return Lode::Value();
    }));

    meta.Set("__metatable", Lode::Value(std::string("ServerResponse")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("ServerResponse"));
    }));

    Lode::ObjectWrap<HttpServerResponse>::Wrap(vm, res, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
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

Lode::Table BuildResponseMethods(Lode::State& vm)
{
    Lode::Table m = vm.CreateTable();

    m.Set("SetStatus", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServerResponse>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ServerResponse SetStatus: invalid response"); return Lode::Value(); }
        if (args.size() > 1 && args[1].IsNumber())
        {
            int code = static_cast<int>(args[1].AsNumber());
            std::string text = (args.size() > 2 && args[2].IsString()) ? args[2].AsString() : "";
            self->SetStatus(code, text);
        }
        return Lode::Value();
    }));

    m.Set("SetHeader", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServerResponse>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ServerResponse SetHeader: invalid response"); return Lode::Value(); }
        if (args.size() > 2 && args[1].IsString() && args[2].IsString())
        {
            self->SetHeader(args[1].AsString(), args[2].AsString());
        }
        return Lode::Value();
    }));

    m.Set("Write", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServerResponse>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ServerResponse Write: invalid response"); return Lode::Value(); }
        if (args.size() > 1)
        {
            if (args[1].IsString())
            {
                std::string s = args[1].AsString();
                self->Write(s.data(), s.size());
            }
            else if (args[1].IsBuffer())
            {
                size_t sz = 0;
                void* ptr = args[1].AsBuffer(&sz);
                if (ptr) self->Write(static_cast<const char*>(ptr), sz);
            }
        }
        return Lode::Value();
    }));

    m.Set("End", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServerResponse>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ServerResponse End: invalid response"); return Lode::Value(); }
        if (args.size() > 1)
        {
            if (args[1].IsString())
            {
                std::string s = args[1].AsString();
                self->End(s.data(), s.size());
            }
            else if (args[1].IsBuffer())
            {
                size_t sz = 0;
                void* ptr = args[1].AsBuffer(&sz);
                self->End(static_cast<const char*>(ptr), sz);
            }
            else
            {
                self->End();
            }
        }
        else
        {
            self->End();
        }
        return Lode::Value();
    }));

    m.Set("Send", m.Get("End").GetValue());

    m.Set("Json", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServerResponse>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("ServerResponse Json: invalid response"); return Lode::Value(); }
        self->SetHeader("Content-Type", "application/json");
        std::string jsonStr = "{}";
        if (args.size() > 1)
        {
            if (args[1].IsString()) jsonStr = args[1].AsString();
        }
        self->End(jsonStr.data(), jsonStr.size());
        return Lode::Value();
    }));

    m.Set("setStatus", m.Get("SetStatus").GetValue());
    m.Set("setHeader", m.Get("SetHeader").GetValue());
    m.Set("write", m.Get("Write").GetValue());
    m.Set("end", m.Get("End").GetValue());
    m.Set("send", m.Get("Send").GetValue());
    m.Set("json", m.Get("Json").GetValue());

    return m;
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

    m.Set("IsListening", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpServer>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("Server LocalAddress: invalid Server"); return Lode::Value(); }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace lodehttp
