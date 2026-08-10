// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Http/HttpClient.hpp"
#include "Lode/Task.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Json.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <charconv>
#include <cctype>

namespace lodehttp
{

static constexpr size_t kMaxHeaderSize = 64 * 1024;
static constexpr size_t kMaxResponseSize = 512ull * 1024 * 1024;
static constexpr size_t kMaxTrailersSize = 8 * 1024;

HttpRequestContext::HttpRequestContext(std::shared_ptr<HttpClient> c)
    : client(std::move(c))
{
    std::memset(&addrReq, 0, sizeof(addrReq));
    std::memset(&tcp, 0, sizeof(tcp));
    std::memset(&connReq, 0, sizeof(connReq));
    std::memset(&writeReq, 0, sizeof(writeReq));
    std::memset(&timer, 0, sizeof(timer));
    result = std::make_shared<HttpResponseData>();
}

HttpRequestContext::~HttpRequestContext()
{
    CloseHandles();
}

std::string HttpRequestContext::HeaderValue(const std::string& name) const
{
    for (const auto& h : headers)
    {
        if (h.name == name)
            return h.value;
    }
    return std::string();
}

void HttpRequestContext::CloseHandles()
{
    if (closed)
        return;
    closed = true;
    if (reading)
    {
        uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp));
        reading = false;
    }
    if (tcpInited && !tcpClosed)
    {
        tcpClosed = true;
        uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
        ++closeCount;
    }
    if (timerInited && !timerClosed)
    {
        timerClosed = true;
        uv_timer_stop(&timer);
        uv_close(reinterpret_cast<uv_handle_t*>(&timer), OnHandleClosed);
        ++closeCount;
    }
    CheckAllClosed();
}

void HttpRequestContext::CheckAllClosed()
{
    if (!closed || closeCount != 0 || !dnsDone)
        return;
    OnAllClosed();
}

void HttpRequestContext::OnAllClosed()
{
    if (client)
    {
        client->RemoveRequest(shared_from_this());
    }
    if (taskCtx.IsValid())
    {
        if (!result->errorKind.empty())
            taskCtx.ResumeError(result->errorKind + ": " + result->errorMessage);
        else
        {
            Lode::State vm(client->mainL);
            Lode::Table resTable = client ? client->BuildResponseTable(vm, result) : vm.CreateTable();
            taskCtx.Resume({ Lode::Value(resTable) });
        }
        taskCtx = Lode::Coroutine();
    }
    else if (isAsync && client)
    {
        if (!result->errorKind.empty())
            client->FireError(result->errorKind + ": " + result->errorMessage);
        else
            client->FireResponse(result);
    }
    selfGuard.reset();
}

void HttpRequestContext::OnHandleClosed(uv_handle_t* handle)
{
    auto* self = static_cast<HttpRequestContext*>(handle->data);
    if (!self) return;
    self->closeCount -= 1;
    self->CheckAllClosed();
}

void HttpRequestContext::FinishError(const std::string& kind, const std::string& detail)
{
    if (requestComplete) return;
    requestComplete = true;
    result->errorKind = kind;
    result->errorMessage = detail;
    CloseHandles();
}

void HttpRequestContext::FinishSuccess()
{
    if (requestComplete) return;
    requestComplete = true;
    result->status = status;
    result->statusText = statusText;
    result->version = version;
    result->headers = headers;
    if (bodyMode != BodyMode::Chunked)
        result->body = raw.substr(headerEnd + 4);
    result->finalUrl = url.scheme + "://" + url.authority + url.path;
    CloseHandles();
}

void HttpRequestContext::BuildRequestText()
{
    // 1. Garante que o método seja sempre maiúsculo e não nulo
    std::string method = opts.method.empty() ? "GET" : opts.method;
    std::transform(method.begin(), method.end(), method.begin(), ::toupper);

    // 2. Trata path vazio apontando para a raiz "/"
    std::string path = url.path.empty() ? "/" : url.path;

    std::string head = method + " " + path + " HTTP/1.1\r\n";

    bool hasHost = false;
    bool hasConnection = false;
    bool hasContentLength = false;

    // Função aux para comparar headers ignorando maiúsculas/minúsculas
    auto toLower = [](std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };

    // 3. Primeira passagem: verifica existência dos headers
    for (const auto &h : opts.headers)
    {
        std::string lowerName = toLower(h.name);
        if (lowerName == "host")
            hasHost = true;
        else if (lowerName == "connection")
            hasConnection = true;
        else if (lowerName == "content-length")
            hasContentLength = true;
    }

    // 4. Segunda passagem: adiciona os demais headers
    for (const auto &h : opts.headers)
    {
        std::string lowerName = toLower(h.name);
        if (lowerName == "host" || lowerName == "connection" ||
            lowerName == "content-length" || lowerName == "transfer-encoding")
        {
            continue;
        }
        head += h.name + ": " + h.value + "\r\n";
    }

    // 5. Injeta headers obrigatórios ausentes
    if (!hasHost)
        head += "Host: " + url.authority + "\r\n";
    if (!hasConnection)
        head += "Connection: close\r\n";
    if (!hasContentLength && !opts.body.empty())
        head += "Content-Length: " + std::to_string(opts.body.size()) + "\r\n";

    head += "\r\n";
    head += opts.body;
    requestText = std::move(head);
}

// Basic Parsing implementation to replace ParseInt64 and ParseHex locally if needed
namespace {
    bool ParseInt64Local(const std::string& s, int64_t& out) {
        if (s.empty()) return false;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
        return ec == std::errc() && ptr == s.data() + s.size();
    }
    int64_t ParseHexLocal(const std::string& s) {
        int64_t v = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
        if (ec != std::errc()) return -1;
        return v;
    }
    std::string Trim(const std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t')) ++start;
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
        return s.substr(start, end - start);
    }
    std::string ToLowerAsciiLocal(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

bool HttpRequestContext::ParseHeaders()
{
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos || lineEnd == 0) { FinishError("malformed-response", "invalid status line"); return false; }
    std::string statusLine = raw.substr(0, lineEnd);
    if (statusLine.rfind("HTTP/", 0) != 0) { FinishError("malformed-response", "invalid status line"); return false; }
    size_t sp1 = statusLine.find(' ');
    if (sp1 == std::string::npos) { FinishError("malformed-response", "invalid status line"); return false; }
    version = statusLine.substr(0, sp1);
    size_t sp2 = statusLine.find(' ', sp1 + 1);
    std::string codeStr = sp2 == std::string::npos ? statusLine.substr(sp1 + 1) : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);
    int64_t code = 0;
    if (!ParseInt64Local(codeStr, code) || code < 100 || code > 999) { FinishError("malformed-response", "invalid status code"); return false; }
    status = static_cast<int>(code);
    statusText = sp2 == std::string::npos ? std::string() : statusLine.substr(sp2 + 1);

    size_t p = lineEnd + 2;
    while (p < headerEnd)
    {
        size_t e = raw.find("\r\n", p);
        if (e == std::string::npos || e > headerEnd) break;
        std::string line = raw.substr(p, e - p);
        size_t colon = line.find(':');
        if (colon == std::string::npos) { FinishError("malformed-response", "invalid header line"); return false; }
        std::string name = ToLowerAsciiLocal(Trim(line.substr(0, colon)));
        std::string value = Trim(line.substr(colon + 1));
        if (name.empty()) { FinishError("malformed-response", "invalid header line"); return false; }
        headers.push_back({name, value});
        p = e + 2;
    }

    std::string transferEncoding = ToLowerAsciiLocal(HeaderValue("transfer-encoding"));
    if (transferEncoding.find("chunked") != std::string::npos)
    {
        bodyMode = BodyMode::Chunked;
        bodyState = BodyState::SizeLine;
    }
    else
    {
        std::string cl = HeaderValue("content-length");
        if (!cl.empty())
        {
            if (cl.find(',') != std::string::npos || !ParseInt64Local(cl, contentLength) || contentLength < 0)
            {
                FinishError("malformed-response", "invalid content-length");
                return false;
            }
            bodyMode = BodyMode::ContentLength;
        }
    }
    return true;
}

void HttpRequestContext::CompleteResponse()
{
    if (requestComplete) return;
    bool isRedirect = status >= 300 && status < 400;
    std::string location = isRedirect ? HeaderValue("location") : std::string();
    if (isRedirect && !location.empty() && opts.followRedirects && redirectsDone < opts.maxRedirects)
    {
        std::string next = ResolveRedirect(url.scheme + "://" + url.authority + url.path, location);
        if (!next.empty())
        {
            ++redirectsDone;
            // Create a new request based on the redirect
            CloseHandles();
            auto newReq = std::make_shared<HttpRequestContext>(client);
            newReq->opts = opts;
            newReq->redirectsDone = redirectsDone;
            newReq->taskCtx = taskCtx;
            newReq->isAsync = isAsync;
            newReq->selfGuard = newReq;
            client->activeRequests.push_back(newReq);
            newReq->Begin(next);
            return;
        }
    }
    FinishSuccess();
}

void HttpRequestContext::TryParse()
{
    if (requestComplete) return;
    if (headerEnd == std::string::npos)
    {
        size_t idx = raw.find("\r\n\r\n");
        if (idx == std::string::npos)
        {
            if (raw.size() > kMaxHeaderSize) FinishError("malformed-response", "headers too large");
            return;
        }
        headerEnd = idx;
        if (!ParseHeaders()) return;
        pos = headerEnd + 4;
    }

    if (bodyMode == BodyMode::Chunked)
    {
        while (!requestComplete)
        {
            if (bodyState == BodyState::SizeLine)
            {
                size_t crlf = raw.find("\r\n", pos);
                if (crlf == std::string::npos) return;
                std::string line = raw.substr(pos, crlf - pos);
                size_t semi = line.find(';');
                if (semi != std::string::npos) line = line.substr(0, semi);
                int64_t size = ParseHexLocal(Trim(line));
                if (size < 0) { FinishError("malformed-response", "invalid chunk size"); return; }
                pos = crlf + 2;
                if (size == 0) bodyState = BodyState::Trailers;
                else { chunkStart = pos; chunkRemaining = size; bodyState = BodyState::ChunkData; }
            }
            else if (bodyState == BodyState::ChunkData)
            {
                if (raw.size() - chunkStart < static_cast<size_t>(chunkRemaining)) return;
                result->body.append(raw.data() + chunkStart, static_cast<size_t>(chunkRemaining));
                pos = chunkStart + static_cast<size_t>(chunkRemaining);
                bodyState = BodyState::ChunkCrlf;
            }
            else if (bodyState == BodyState::ChunkCrlf)
            {
                if (raw.size() - pos < 2) return;
                if (raw.compare(pos, 2, "\r\n") != 0) { FinishError("malformed-response", "invalid chunk terminator"); return; }
                pos += 2;
                bodyState = BodyState::SizeLine;
            }
            else
            {
                size_t end = raw.find("\r\n", pos);
                if (end != std::string::npos) { CompleteResponse(); return; }
                if (raw.size() - pos > kMaxTrailersSize) { FinishError("malformed-response", "trailers too large"); return; }
                return;
            }
        }
        return;
    }

    if (bodyMode == BodyMode::ContentLength)
    {
        size_t bodyEnd = headerEnd + 4 + static_cast<size_t>(contentLength);
        if (raw.size() >= bodyEnd)
        {
            raw.resize(bodyEnd);
            CompleteResponse();
        }
        return;
    }
}

void HttpRequestContext::OnEof()
{
    if (requestComplete) return;
    if (headerEnd == std::string::npos) { FinishError("malformed-response", "connection closed before response headers"); return; }
    if (bodyMode == BodyMode::Chunked) { FinishError("malformed-response", "connection closed before chunked response completed"); return; }
    if (bodyMode == BodyMode::ContentLength && raw.size() < headerEnd + 4 + static_cast<size_t>(contentLength)) { FinishError("malformed-response", "connection closed before body completed"); return; }
    CompleteResponse();
}

std::string HttpRequestContext::Begin(const std::string& targetUrl)
{
    url = ParseUrl(targetUrl);
    if (!url.valid)
    {
        dnsDone = true;
        return "invalid-url: " + url.error;
    }

#ifdef _WIN32
    if (url.scheme == "https")
    {
        tls = std::make_unique<TlsContext>();
        if (!tls->Init())
        {
            dnsDone = true;
            return "tls: failed to acquire SSPI credentials";
        }
    }
#else
    if (url.scheme == "https")
    {
        dnsDone = true;
        return "tls: HTTPS is not supported on this platform";
    }
#endif

    loop = client->loop;
    addrInited = true;
    addrReq.data = this;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    std::string portStr = std::to_string(url.port);
    int r = uv_getaddrinfo(loop, &addrReq, OnResolved, url.host.c_str(), portStr.c_str(), &hints);
    if (r != 0)
    {
        dnsDone = true;
        return "dns: " + std::string(uv_strerror(r));
    }

    if (opts.timeoutMs > 0)
    {
        timerInited = true;
        timer.data = this;
        uv_timer_init(loop, &timer);
        uv_timer_start(&timer, OnTimeout, opts.timeoutMs, 0);
    }
    return "";
}

void HttpRequestContext::OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
{
    auto* self = static_cast<HttpRequestContext*>(req->data);
    self->dnsDone = true;
    if (self->requestComplete)
    {
        if (res) uv_freeaddrinfo(res);
        self->CheckAllClosed();
        return;
    }
    if (status != 0)
    {
        if (res) uv_freeaddrinfo(res);
        self->FinishError("dns", uv_strerror(status));
        return;
    }

    struct sockaddr_storage addr;
    std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
    uv_freeaddrinfo(res);
    if (self->requestComplete) return;

    // Only pre-build the plain-text request if NOT using TLS.
    // For HTTPS, BuildRequestText() is called after the handshake completes.
#ifdef _WIN32
    if (!self->tls)
        self->BuildRequestText();
#else
    self->BuildRequestText();
#endif
    self->tcpInited = true;
    self->tcp.data = self;
    uv_tcp_init(self->loop, &self->tcp);
    uv_tcp_nodelay(&self->tcp, 1);
    self->connReq.data = self;
    uv_tcp_connect(&self->connReq, &self->tcp, reinterpret_cast<const struct sockaddr*>(&addr), OnConnected);
}

void HttpRequestContext::OnConnected(uv_connect_t* req, int status)
{
    auto* self = static_cast<HttpRequestContext*>(req->data);
    if (self->requestComplete) return;
    if (status != 0)
    {
        std::string detail = uv_strerror(status);
        std::string kind = (status == UV_ETIMEDOUT) ? "timeout" : "connection-refused";
        self->FinishError(kind, "connect " + detail);
        return;
    }

#ifdef _WIN32
    if (self->tls)
    {
        // Start TLS handshake: generate ClientHello and send it.
        self->tlsWriteBuffer = self->tls->StartHandshake(self->url.host);
        if (self->tlsWriteBuffer.empty())
        {
            self->FinishError("tls", "StartHandshake returned empty ClientHello");
            return;
        }
        self->tlsHandshaking = true;
        self->writeReq.data  = self;
        self->writeBuf.base  = reinterpret_cast<char*>(self->tlsWriteBuffer.data());
        self->writeBuf.len   = static_cast<decltype(self->writeBuf.len)>(self->tlsWriteBuffer.size());
        uv_write(&self->writeReq, reinterpret_cast<uv_stream_t*>(&self->tcp), &self->writeBuf, 1, OnWritten);
        return;
    }
#endif

    self->writeReq.data = self;
    self->writeBuf.base = self->requestText.data();
    self->writeBuf.len  = static_cast<decltype(self->writeBuf.len)>(self->requestText.size());
    uv_write(&self->writeReq, reinterpret_cast<uv_stream_t*>(&self->tcp), &self->writeBuf, 1, OnWritten);
}

void HttpRequestContext::OnWritten(uv_write_t* req, int status)
{
    auto* self = static_cast<HttpRequestContext*>(req->data);
    if (self->requestComplete) return;
    if (status != 0)
    {
        self->FinishError("connection-refused", std::string("write ") + uv_strerror(status));
        return;
    }
    self->reading = true;
    uv_read_start(reinterpret_cast<uv_stream_t*>(&self->tcp), AllocBuffer, OnRead);
}

void HttpRequestContext::OnTimeout(uv_timer_t* timer)
{
    auto* self = static_cast<HttpRequestContext*>(timer->data);
    if (self->requestComplete) return;
    self->FinishError("timeout", "request timed out");
}

void HttpRequestContext::AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle; (void)suggestedSize;
    buf->base = new char[64 * 1024];
    buf->len = 64 * 1024;
}

void HttpRequestContext::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    auto* self = static_cast<HttpRequestContext*>(stream->data);
    if (nread > 0)
    {
#ifdef _WIN32
        if (self->tls)
        {
            if (self->tlsHandshaking)
            {
                // Feed bytes to the SChannel handshake state machine.
                std::vector<uint8_t> outSend;
                std::string errOut;
                auto result = self->tls->ProcessHandshakeData(
                    reinterpret_cast<const uint8_t*>(buf->base),
                    static_cast<size_t>(nread),
                    outSend, errOut);

                delete[] buf->base;

                switch (result)
                {
                case TlsContext::HandshakeResult::NeedMoreData:
                    // Keep reading; SChannel needs more bytes.
                    return;

                case TlsContext::HandshakeResult::DataToSend:
                    // Stop reading, send handshake reply, then OnWritten restarts reading.
                    uv_read_stop(reinterpret_cast<uv_stream_t*>(&self->tcp));
                    self->reading = false;
                    self->tlsWriteBuffer = std::move(outSend);
                    self->writeReq.data  = self;
                    self->writeBuf.base  = reinterpret_cast<char*>(self->tlsWriteBuffer.data());
                    self->writeBuf.len   = static_cast<decltype(self->writeBuf.len)>(self->tlsWriteBuffer.size());
                    uv_write(&self->writeReq, reinterpret_cast<uv_stream_t*>(&self->tcp),
                             &self->writeBuf, 1, OnWritten);
                    return;

                case TlsContext::HandshakeResult::Complete:
                {
                    self->tlsHandshaking = false;
                    self->tls->DrainPending(); // discard session tickets / early data

                    // Build and encrypt the HTTP request now that TLS is ready.
                    self->BuildRequestText();
                    std::string encErr;
                    self->tlsWriteBuffer = self->tls->Encrypt(
                        self->requestText.data(), self->requestText.size(), encErr);
                    if (!encErr.empty() || self->tlsWriteBuffer.empty())
                    {
                        self->FinishError("tls", encErr.empty() ? "Encrypt returned empty" : encErr);
                        return;
                    }
                    // Stop reading, send encrypted request, OnWritten restarts reading.
                    uv_read_stop(reinterpret_cast<uv_stream_t*>(&self->tcp));
                    self->reading        = false;
                    self->writeReq.data  = self;
                    self->writeBuf.base  = reinterpret_cast<char*>(self->tlsWriteBuffer.data());
                    self->writeBuf.len   = static_cast<decltype(self->writeBuf.len)>(self->tlsWriteBuffer.size());
                    uv_write(&self->writeReq, reinterpret_cast<uv_stream_t*>(&self->tcp),
                             &self->writeBuf, 1, OnWritten);
                    return;
                }

                case TlsContext::HandshakeResult::Error:
                    self->FinishError("tls", errOut);
                    return;
                }
            }
            else
            {
                // Application data phase: decrypt, then feed to the HTTP parser.
                std::string decErr;
                auto decrypted = self->tls->Decrypt(
                    reinterpret_cast<const uint8_t*>(buf->base),
                    static_cast<size_t>(nread),
                    decErr);
                delete[] buf->base;

                if (!decErr.empty())
                {
                    self->FinishError("tls", decErr);
                    return;
                }
                if (!decrypted.empty())
                {
                    if (self->raw.size() + decrypted.size() > kMaxResponseSize)
                        self->FinishError("malformed-response", "response exceeds max size");
                    else
                    {
                        self->raw.append(reinterpret_cast<char*>(decrypted.data()), decrypted.size());
                        self->TryParse();
                    }
                }
                return;
            }
        }
#endif
        // Plain HTTP (no TLS).
        if (self->raw.size() + static_cast<size_t>(nread) > kMaxResponseSize)
        {
            self->FinishError("malformed-response", "response exceeds max size");
        }
        else
        {
            self->raw.append(buf->base, static_cast<size_t>(nread));
            self->TryParse();
        }
    }
    else if (nread == UV_EOF)
    {
        self->OnEof();
    }
    else if (nread < 0)
    {
        self->FinishError("connection-refused", uv_strerror(static_cast<int>(nread)));
    }
    delete[] buf->base;
}

void HttpClient::InitSignals(Lode::State& vm)
{
    responseSig = Lode::Signal::Create(vm);
    errorSig = Lode::Signal::Create(vm);
    responseProxy = responseSig->CreatePublic();
    errorProxy = errorSig->CreatePublic();
}

void HttpClient::FireError(const std::string& message)
{
    if (closed) return;
    std::vector<Lode::Value> args = { Lode::Value(message) };
    errorSig->Fire(args);
}

Lode::Table HttpClient::BuildResponseTable(Lode::State& vm, const std::shared_ptr<HttpResponseData>& res)
{
    Lode::Table t = vm.CreateTable();
    t.Set("status", Lode::Value(static_cast<double>(res->status)));
    t.Set("statusText", Lode::Value(res->statusText));
    t.Set("version", Lode::Value(res->version));
    t.Set("url", Lode::Value(res->finalUrl));
    
    Lode::Table headers = vm.CreateTable();
    for (const auto& h : res->headers)
    {
        headers.Set(h.name, Lode::Value(h.value));
    }
    t.Set("headers", Lode::Value(headers));

    Lode::Table bodyObj = vm.CreateTable();
    std::string rawBody = res->body;

    bodyObj.Set("AsTable", vm.CreateFunction([rawBody](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto parsed = Lode::Json::Parse(vm2, rawBody);
        if (parsed.IsError()) { vm2.RaiseError(parsed.GetError().ErrorMessage()); return Lode::Value(); }
        return parsed.GetValue();
    }));

    bodyObj.Set("AsJson", vm.CreateFunction([rawBody](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.size() > 1 && !args[1].IsNil() && !args[1].IsBoolean()) {
            vm2.RaiseError("expected pretty as boolean");
            return Lode::Value();
        }
        bool pretty = args.size() > 1 && args[1].IsBoolean() ? args[1].AsBoolean() : false;
        
        auto parsed = Lode::Json::Parse(vm2, rawBody);
        if (parsed.IsError()) {
            std::string errMessage = "Cannot format non-JSON body as JSON. Raw body: " + rawBody;
            vm2.RaiseError(errMessage.c_str());
            return Lode::Value();
        }
        
        auto encoded = Lode::Json::Stringify(parsed.GetValue(), pretty);
        if (encoded.IsError()) { 
            vm2.RaiseError(encoded.GetError().ErrorMessage()); 
            return Lode::Value(); 
        }
        
        return Lode::Value(encoded.GetValue());
    }));

    Lode::Table bodyMeta = vm.CreateTable();
    bodyMeta.Set("__tostring", vm.CreateFunction([rawBody](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(rawBody);
    }));
    bodyObj.SetMetatable(bodyMeta);

    t.Set("body", Lode::Value(bodyObj));

    return t;
}

void HttpClient::FireResponse(const std::shared_ptr<HttpResponseData>& res)
{
    if (closed) return;
    Lode::State vm(mainL);
    Lode::Table t = BuildResponseTable(vm, res);
    std::vector<Lode::Value> args = { Lode::Value(t) };
    responseSig->Fire(args);
}

void HttpClient::RemoveRequest(const std::shared_ptr<HttpRequestContext>& req)
{
    auto it = std::find(activeRequests.begin(), activeRequests.end(), req);
    if (it != activeRequests.end())
        activeRequests.erase(it);
}

void HttpClient::RequestClose()
{
    if (closed) return;
    closed = true;
    auto copy = activeRequests;
    for (auto& req : copy)
    {
        req->FinishError("aborted", "client closed");
    }
    mgr->RemoveClient(shared_from_this());
}

Lode::Value HttpClient::MethodClose(Lode::State& vm)
{
    RequestClose();
    return Lode::Value();
}

Lode::Value HttpClient::MethodRequestAsync(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed) { vm.RaiseError("HttpClient is closed"); return Lode::Value(); }
    if (args.empty() || !args[0].IsString()) { vm.RaiseError("Expected url as string"); return Lode::Value(); }
    
    std::string url = args[0].AsString();
    auto req = std::make_shared<HttpRequestContext>(shared_from_this());
    req->isAsync = true;
    req->selfGuard = req;

    if (args.size() > 1)
    {
        if (!ParseFetchOptions(vm, args[1], req->opts))
        {
            req->taskCtx = Lode::Coroutine();
            activeRequests.pop_back();
            return Lode::Value();
        }
    }

    activeRequests.push_back(req);

    std::string err = req->Begin(url);
    if (!err.empty())
    {
        req->taskCtx = Lode::Coroutine();
        activeRequests.pop_back();
        FireError(err);
        return Lode::Value();
    }
    return Lode::Value();
}

Lode::Value HttpClient::MethodRequest(Lode::State& vm, const std::vector<Lode::Value>& args)
{
    if (closed) { vm.RaiseError("HttpClient is closed"); return Lode::Value(); }
    if (args.empty() || !args[0].IsString()) { vm.RaiseError("Expected url as string"); return Lode::Value(); }

    std::string url = args[0].AsString();
    auto req = std::make_shared<HttpRequestContext>(shared_from_this());
    req->isAsync = false;
    req->selfGuard = req;

    if (args.size() > 1)
    {
        if (!ParseFetchOptions(vm, args[1], req->opts))
        {
            req->taskCtx = Lode::Coroutine();
            activeRequests.pop_back();
            return Lode::Value();
        }
    }

    activeRequests.push_back(req);

    // Suspend coroutine via TaskContext!
    req->taskCtx = Lode::Coroutine(vm.GetLuaState());

    std::string err = req->Begin(url);
    if (!err.empty())
    {
        req->taskCtx = Lode::Coroutine();
        activeRequests.pop_back();
        vm.RaiseError(err);
        return Lode::Value();
    }

    // Yield
    return vm.YieldThread();
}

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<HttpClient>& client, const Lode::Table& methods)
{
    Lode::Table meta = vm.CreateTable();
    meta.Set("__index", vm.CreateFunction([client, methods](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        std::string key = (args.size() > 1 && args[1].IsString()) ? args[1].AsString() : "";
        if (key == "ResponseReceived") return client->responseProxy;
        if (key == "ErrorOccurred") return client->errorProxy;
        auto value = methods.Get(key);
        if (value.IsOk() && !value.GetValue().IsNil()) return value.GetValue();
        return Lode::Value();
    }));
    meta.Set("__newindex", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        vm2.RaiseError("HttpClient: objects are read-only");
        return Lode::Value();
    }));
    meta.Set("__metatable", Lode::Value(std::string("HttpClient")));
    meta.Set("__tostring", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        return Lode::Value(std::string("HttpClient"));
    }));
    Lode::ObjectWrap<HttpClient>::Wrap(vm, client, meta);
    Lode::Value value = vm.GetValue(-1);
    vm.Pop(1);
    return value;
}

Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<HttpManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Request", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpClient>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("HttpClient Request: invalid HttpClient"); return Lode::Value(); }
        if (mgr->shuttingDown) { vm2.RaiseError("HttpClient Request: runtime is shutting down"); return Lode::Value(); }
        // args[0] is self, args[1...] is real arguments
        std::vector<Lode::Value> actualArgs(args.begin() + 1, args.end());
        return self->MethodRequest(vm2, actualArgs);
    }));

    m.Set("RequestAsync", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpClient>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("HttpClient RequestAsync: invalid HttpClient"); return Lode::Value(); }
        if (mgr->shuttingDown) { vm2.RaiseError("HttpClient RequestAsync: runtime is shutting down"); return Lode::Value(); }
        std::vector<Lode::Value> actualArgs(args.begin() + 1, args.end());
        return self->MethodRequestAsync(vm2, actualArgs);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<HttpClient>::Unwrap(vm2, 1);
        if (!self) { vm2.RaiseError("HttpClient Close: invalid HttpClient"); return Lode::Value(); }
        self->RequestClose();
        return Lode::Value();
    }));

    return m;
}

} // namespace lodehttp
