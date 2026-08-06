// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "http_client.hpp"

#include "Lode/EventLoop.hpp"
#include "Lode/State.hpp"
#include "Lode/Task.hpp"
#include "Lode/Value.hpp"

#include "uv.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>

namespace lodehttp
{

namespace
{

constexpr size_t kMaxHeaderSize = 64 * 1024;
constexpr size_t kMaxResponseSize = 512ull * 1024 * 1024;
constexpr size_t kMaxTrailersSize = 8 * 1024;

std::string ToLowerAscii(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    size_t start = 0;
    size_t end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t'))
        ++start;
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t'))
        --end;
    return s.substr(start, end - start);
}

bool ParseInt64(const std::string& s, int64_t& out)
{
    if (s.empty())
        return false;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

bool IsSchemeChar(char c)
{
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '+' || c == '-' || c == '.';
}

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int64_t ParseHex(const std::string& s)
{
    int64_t value = 0;
    for (char c : s)
    {
        if (!IsHexDigit(c))
            return -1;
        int digit = (c <= '9') ? (c - '0') : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
        if (value > (INT64_MAX - digit) / 16)
            return -1;
        value = value * 16 + digit;
    }
    return value;
}

void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    (void)handle;
    (void)suggestedSize;
    static constexpr size_t kChunkSize = 64 * 1024;
    buf->base = new char[kChunkSize];
    buf->len = kChunkSize;
}

} // namespace

ParsedUrl ParseUrl(const std::string& raw)
{
    ParsedUrl out;
    size_t schemeEnd = raw.find("://");
    if (schemeEnd == std::string::npos)
    {
        out.error = "URL must include a scheme (e.g. http://example.com)";
        return out;
    }
    if (schemeEnd == 0)
    {
        out.error = "URL must include a scheme name";
        return out;
    }
    std::string scheme = ToLowerAscii(raw.substr(0, schemeEnd));
    for (char c : scheme)
    {
        if (!IsSchemeChar(c))
        {
            out.error = "invalid URL scheme";
            return out;
        }
    }
    out.scheme = scheme;

    size_t restStart = schemeEnd + 3;
    size_t restEnd = raw.find_first_of("/?#", restStart);
    std::string authority = restEnd == std::string::npos ? raw.substr(restStart) : raw.substr(restStart, restEnd - restStart);
    std::string suffix = restEnd == std::string::npos ? std::string() : raw.substr(restEnd);

    if (authority.empty())
    {
        out.error = "URL must include a host";
        return out;
    }

    size_t at = authority.rfind('@');
    if (at != std::string::npos)
        authority = authority.substr(at + 1);

    std::string host;
    std::string portStr;
    if (!authority.empty() && authority.front() == '[')
    {
        size_t close = authority.find(']');
        if (close == std::string::npos)
        {
            out.error = "invalid IPv6 host";
            return out;
        }
        host = authority.substr(1, close - 1);
        std::string rest = authority.substr(close + 1);
        if (!rest.empty())
        {
            if (rest.front() != ':')
            {
                out.error = "invalid host";
                return out;
            }
            portStr = rest.substr(1);
        }
    }
    else
    {
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos)
        {
            host = authority.substr(0, colon);
            portStr = authority.substr(colon + 1);
        }
        else
        {
            host = authority;
        }
    }

    if (host.empty())
    {
        out.error = "URL must include a host";
        return out;
    }
    out.host = ToLowerAscii(host);

    int port = -1;
    if (!portStr.empty())
    {
        int value = 0;
        auto [ptr, ec] = std::from_chars(portStr.data(), portStr.data() + portStr.size(), value);
        if (ec != std::errc() || value < 1 || value > 65535)
        {
            out.error = "invalid port";
            return out;
        }
        port = value;
    }

    if (scheme == "http")
    {
        if (port < 0)
            port = 80;
        out.port = port;
    }
    else if (scheme == "https")
    {
        out.error = "https is not supported yet in this build";
        return out;
    }
    else
    {
        out.error = "unsupported URL scheme: " + scheme;
        return out;
    }

    std::string pathQuery = suffix;
    size_t hash = pathQuery.find('#');
    if (hash != std::string::npos)
        pathQuery = pathQuery.substr(0, hash);
    if (pathQuery.empty())
        pathQuery = "/";
    out.path = pathQuery;

    out.authority = out.host;
    if (!(scheme == "http" && port == 80))
        out.authority += ":" + std::to_string(port);

    out.valid = true;
    return out;
}

std::string ResolveRedirect(const std::string& baseUrl, const std::string& location)
{
    if (location.empty())
        return std::string();
    if (location.find("://") != std::string::npos)
        return location;
    ParsedUrl base = ParseUrl(baseUrl);
    if (!base.valid)
        return location;
    std::string prefix = base.scheme + "://" + base.authority;
    if (location.front() == '/')
        return prefix + location;
    std::string dir = "/";
    size_t slash = base.path.rfind('/');
    if (slash != std::string::npos)
        dir = base.path.substr(0, slash + 1);
    return prefix + dir + location;
}

std::string NormalizeMethod(std::string method)
{
    for (auto& c : method)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return method;
}

struct HttpManager::Session
{
    lua_State* mainL = nullptr;
    std::string url;
    HttpRequestOptions opts;
    std::shared_ptr<HttpResponseData> result;
    std::function<void()> onDone;
    int64_t redirectsDone = 0;
    bool notified = false;
    bool silent = false;
};

struct HttpManager::Request : public std::enable_shared_from_this<Request>
{
    std::shared_ptr<HttpManager> mgr;
    std::shared_ptr<Session> session;
    ParsedUrl url;

    uv_loop_t* loop = nullptr;
    uv_getaddrinfo_t addrReq{};
    uv_tcp_t tcp{};
    uv_connect_t connReq{};
    uv_write_t writeReq{};
    uv_timer_t timer{};
    uv_buf_t writeBuf{};

    bool addrInited = false;
    bool dnsDone = false;
    bool tcpInited = false;
    bool timerInited = false;
    bool tcpClosed = false;
    bool timerClosed = false;
    bool closed = false;
    bool reading = false;
    bool suppressNotify = false;
    bool requestComplete = false;
    int closeCount = 0;

    std::string requestText;
    std::string raw;
    size_t headerEnd = std::string::npos;
    int status = 0;
    std::string statusText;
    std::string version;
    std::vector<HeaderPair> headers;
    enum class BodyMode { None, ContentLength, Chunked } bodyMode = BodyMode::None;
    enum class BodyState { Idle, SizeLine, ChunkData, ChunkCrlf, Trailers } bodyState = BodyState::Idle;
    size_t pos = 0;
    size_t chunkStart = 0;
    int64_t chunkRemaining = 0;
    int64_t contentLength = 0;

    Request(std::shared_ptr<HttpManager> manager, std::shared_ptr<Session> sess)
        : mgr(std::move(manager)), session(std::move(sess))
    {
        std::memset(&addrReq, 0, sizeof(addrReq));
        std::memset(&tcp, 0, sizeof(tcp));
        std::memset(&connReq, 0, sizeof(connReq));
        std::memset(&writeReq, 0, sizeof(writeReq));
        std::memset(&timer, 0, sizeof(timer));
    }

    ~Request()
    {
        CloseHandles();
    }

    std::shared_ptr<Request> selfGuard;

    std::string HeaderValue(const std::string& name) const
    {
        for (const auto& h : headers)
        {
            if (h.name == name)
                return h.value;
        }
        return std::string();
    }

    void NotifyOnce()
    {
        mgr->NotifySession(session);
    }

    void CloseHandles()
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

    void CheckAllClosed()
    {
        if (!closed || closeCount != 0 || !dnsDone)
            return;
        OnAllClosed();
    }

    void OnAllClosed()
    {
        if (!suppressNotify)
            NotifyOnce();
        mgr->Remove(shared_from_this());
        selfGuard.reset();
    }

    static void OnHandleClosed(uv_handle_t* handle)
    {
        auto* self = static_cast<Request*>(handle->data);
        if (!self)
            return;
        self->closeCount -= 1;
        self->CheckAllClosed();
    }

    void FinishError(const std::string& kind, const std::string& detail)
    {
        if (requestComplete)
            return;
        requestComplete = true;
        session->result->errorKind = kind;
        session->result->errorMessage = detail;
        NotifyOnce();
        CloseHandles();
    }

    void FinishSuccess()
    {
        if (requestComplete)
            return;
        requestComplete = true;
        HttpResponseData& res = *session->result;
        res.status = status;
        res.statusText = statusText;
        res.version = version;
        res.headers = headers;
        if (bodyMode != BodyMode::Chunked)
            res.body = raw.substr(headerEnd + 4);
        res.finalUrl = session->url;
        NotifyOnce();
        CloseHandles();
    }

    void BuildRequestText()
    {
        std::string head = session->opts.method + " " + url.path + " HTTP/1.1\r\n";
        bool hasHost = false;
        bool hasConnection = false;
        bool hasContentLength = false;
        for (const auto& h : session->opts.headers)
        {
            if (h.name == "host")
                hasHost = true;
            else if (h.name == "connection")
                hasConnection = true;
            else if (h.name == "content-length")
                hasContentLength = true;
        }
        for (const auto& h : session->opts.headers)
        {
            if (h.name == "host" || h.name == "connection" || h.name == "content-length" || h.name == "transfer-encoding")
                continue;
            head += h.name;
            head += ": ";
            head += h.value;
            head += "\r\n";
        }
        if (!hasHost)
            head += "Host: " + url.authority + "\r\n";
        if (!hasConnection)
            head += "Connection: close\r\n";
        if (!hasContentLength && !session->opts.body.empty())
            head += "Content-Length: " + std::to_string(session->opts.body.size()) + "\r\n";
        head += "\r\n";
        head += session->opts.body;
        requestText = std::move(head);
    }

    bool ParseHeaders()
    {
        size_t lineEnd = raw.find("\r\n");
        if (lineEnd == std::string::npos || lineEnd == 0)
        {
            FinishError("malformed-response", "invalid status line");
            return false;
        }
        std::string statusLine = raw.substr(0, lineEnd);
        if (statusLine.rfind("HTTP/", 0) != 0)
        {
            FinishError("malformed-response", "invalid status line");
            return false;
        }
        size_t sp1 = statusLine.find(' ');
        if (sp1 == std::string::npos)
        {
            FinishError("malformed-response", "invalid status line");
            return false;
        }
        version = statusLine.substr(0, sp1);
        size_t sp2 = statusLine.find(' ', sp1 + 1);
        std::string codeStr = sp2 == std::string::npos ? statusLine.substr(sp1 + 1) : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);
        int64_t code = 0;
        if (!ParseInt64(codeStr, code) || code < 100 || code > 999)
        {
            FinishError("malformed-response", "invalid status code");
            return false;
        }
        status = static_cast<int>(code);
        statusText = sp2 == std::string::npos ? std::string() : statusLine.substr(sp2 + 1);

        size_t p = lineEnd + 2;
        while (p < headerEnd)
        {
            size_t e = raw.find("\r\n", p);
            if (e == std::string::npos || e > headerEnd)
                break;
            std::string line = raw.substr(p, e - p);
            size_t colon = line.find(':');
            if (colon == std::string::npos)
            {
                FinishError("malformed-response", "invalid header line");
                return false;
            }
            std::string name = ToLowerAscii(Trim(line.substr(0, colon)));
            std::string value = Trim(line.substr(colon + 1));
            if (name.empty())
            {
                FinishError("malformed-response", "invalid header line");
                return false;
            }
            headers.push_back({name, value});
            p = e + 2;
        }

        std::string transferEncoding = ToLowerAscii(HeaderValue("transfer-encoding"));
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
                if (cl.find(',') != std::string::npos || !ParseInt64(cl, contentLength) || contentLength < 0)
                {
                    FinishError("malformed-response", "invalid content-length");
                    return false;
                }
                bodyMode = BodyMode::ContentLength;
                bodyState = BodyState::Idle;
            }
            else
            {
                bodyMode = BodyMode::None;
            }
        }
        return true;
    }

    void CompleteResponse()
    {
        if (requestComplete)
            return;
        bool isRedirect = status >= 300 && status < 400;
        std::string location = isRedirect ? HeaderValue("location") : std::string();
        if (isRedirect && !location.empty() && session->opts.followRedirects &&
            session->redirectsDone < session->opts.maxRedirects)
        {
            std::string next = ResolveRedirect(session->url, location);
            if (!next.empty())
            {
                ++session->redirectsDone;
                suppressNotify = true;
                requestComplete = true;
                CloseHandles();
                mgr->StartSession(session, next);
                return;
            }
        }
        FinishSuccess();
    }

    void TryParse()
    {
        if (requestComplete)
            return;
        if (headerEnd == std::string::npos)
        {
            size_t idx = raw.find("\r\n\r\n");
            if (idx == std::string::npos)
            {
                if (raw.size() > kMaxHeaderSize)
                    FinishError("malformed-response", "response headers exceed the maximum allowed size");
                return;
            }
            headerEnd = idx;
            if (!ParseHeaders())
                return;
            pos = headerEnd + 4;
        }

        if (bodyMode == BodyMode::Chunked)
        {
            while (!requestComplete)
            {
                if (bodyState == BodyState::SizeLine)
                {
                    size_t crlf = raw.find("\r\n", pos);
                    if (crlf == std::string::npos)
                        return;
                    std::string line = raw.substr(pos, crlf - pos);
                    size_t semi = line.find(';');
                    if (semi != std::string::npos)
                        line = line.substr(0, semi);
                    int64_t size = ParseHex(Trim(line));
                    if (size < 0)
                    {
                        FinishError("malformed-response", "invalid chunk size");
                        return;
                    }
                    pos = crlf + 2;
                    if (size == 0)
                    {
                        bodyState = BodyState::Trailers;
                    }
                    else
                    {
                        chunkStart = pos;
                        chunkRemaining = size;
                        bodyState = BodyState::ChunkData;
                    }
                }
                else if (bodyState == BodyState::ChunkData)
                {
                    if (raw.size() - chunkStart < static_cast<size_t>(chunkRemaining))
                        return;
                    session->result->body.append(raw.data() + chunkStart, static_cast<size_t>(chunkRemaining));
                    pos = chunkStart + static_cast<size_t>(chunkRemaining);
                    bodyState = BodyState::ChunkCrlf;
                }
                else if (bodyState == BodyState::ChunkCrlf)
                {
                    if (raw.size() - pos < 2)
                        return;
                    if (raw.compare(pos, 2, "\r\n") != 0)
                    {
                        FinishError("malformed-response", "invalid chunk terminator");
                        return;
                    }
                    pos += 2;
                    bodyState = BodyState::SizeLine;
                }
                else
                {
                    size_t end = raw.find("\r\n", pos);
                    if (end != std::string::npos)
                    {
                        CompleteResponse();
                        return;
                    }
                    if (raw.size() - pos > kMaxTrailersSize)
                    {
                        FinishError("malformed-response", "chunked trailers exceed the maximum allowed size");
                        return;
                    }
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
                raw.resize(headerEnd + 4 + static_cast<size_t>(contentLength));
                CompleteResponse();
            }
            return;
        }
    }

    void OnEof()
    {
        if (requestComplete)
            return;
        if (headerEnd == std::string::npos)
        {
            FinishError("malformed-response", "connection closed before response headers");
            return;
        }
        if (bodyMode == BodyMode::Chunked)
        {
            FinishError("malformed-response", "connection closed before chunked response completed");
            return;
        }
        if (bodyMode == BodyMode::ContentLength && raw.size() < headerEnd + 4 + static_cast<size_t>(contentLength))
        {
            FinishError("malformed-response", "connection closed before response body completed");
            return;
        }
        CompleteResponse();
    }

    void Begin()
    {
        Lode::State vm(session->mainL);
        loop = vm.GetEventLoop().GetUVLoop();

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
            FinishError("dns", uv_strerror(r));
            return;
        }

        if (session->opts.timeoutMs > 0)
        {
            timerInited = true;
            timer.data = this;
            uv_timer_init(loop, &timer);
            uv_timer_start(&timer, OnTimeout, session->opts.timeoutMs, 0);
        }
    }

    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
    {
        auto* self = static_cast<Request*>(req->data);
        self->dnsDone = true;
        if (self->requestComplete)
        {
            if (res)
                uv_freeaddrinfo(res);
            self->CheckAllClosed();
            return;
        }
        if (status != 0)
        {
            if (res)
                uv_freeaddrinfo(res);
            self->FinishError("dns", uv_strerror(status));
            return;
        }

        struct sockaddr_storage addr;
        std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
        uv_freeaddrinfo(res);
        if (self->requestComplete)
            return;

        self->BuildRequestText();
        self->tcpInited = true;
        self->tcp.data = self;
        uv_tcp_init(self->loop, &self->tcp);
        uv_tcp_nodelay(&self->tcp, 1);
        self->connReq.data = self;
        uv_tcp_connect(&self->connReq, &self->tcp, reinterpret_cast<const struct sockaddr*>(&addr), OnConnected);
    }

    static void OnConnected(uv_connect_t* req, int status)
    {
        auto* self = static_cast<Request*>(req->data);
        if (self->requestComplete)
            return;
        if (status != 0)
        {
            std::string detail = uv_strerror(status);
            std::string kind = "connection-refused";
            if (status == UV_ETIMEDOUT)
                kind = "timeout";
            self->FinishError(kind, std::string("connect ") + detail + " (" + self->url.host + ":" + std::to_string(self->url.port) + ")");
            return;
        }
        self->writeReq.data = self;
        self->writeBuf.base = self->requestText.data();
        self->writeBuf.len = self->requestText.size();
        uv_write(&self->writeReq, reinterpret_cast<uv_stream_t*>(&self->tcp), &self->writeBuf, 1, OnWritten);
    }

    static void OnWritten(uv_write_t* req, int status)
    {
        auto* self = static_cast<Request*>(req->data);
        if (self->requestComplete)
            return;
        if (status != 0)
        {
            self->FinishError("connection-refused", std::string("write ") + uv_strerror(status));
            return;
        }
        self->reading = true;
        uv_read_start(reinterpret_cast<uv_stream_t*>(&self->tcp), AllocBuffer, OnRead);
    }

    static void OnTimeout(uv_timer_t* timer)
    {
        auto* self = static_cast<Request*>(timer->data);
        if (self->requestComplete)
            return;
        self->FinishError("timeout", "request timed out after " + std::to_string(self->session->opts.timeoutMs) + " ms");
    }

    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
    {
        auto* self = static_cast<Request*>(stream->data);
        if (nread > 0)
        {
            if (self->raw.size() + static_cast<size_t>(nread) > kMaxResponseSize)
            {
                self->FinishError("malformed-response", "response exceeds the maximum allowed size");
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
            if (nread != UV_ECANCELED)
                self->FinishError("connection-refused", std::string("read ") + uv_strerror(static_cast<int>(nread)));
        }
        delete[] buf->base;
    }
};

HttpManager::HttpManager(lua_State* mainL) : mainL_(mainL) {}

HttpManager::~HttpManager()
{
    std::vector<std::shared_ptr<Request>> snapshot = std::move(requests_);
    requests_.clear();
    for (auto& request : snapshot)
    {
        request->session->silent = true;
        request->CloseHandles();
    }
}

void HttpManager::Start(const std::string& url, const HttpRequestOptions& opts,
                        const std::shared_ptr<HttpResponseData>& result,
                        std::function<void()> onDone)
{
    auto session = std::make_shared<Session>();
    session->mainL = mainL_;
    session->url = url;
    session->opts = opts;
    session->result = result;
    session->onDone = std::move(onDone);
    StartSession(session, url);
}

void HttpManager::StartSession(const std::shared_ptr<Session>& session, const std::string& url)
{
    session->url = url;
    ParsedUrl parsed = ParseUrl(url);
    if (!parsed.valid)
    {
        std::string kind = parsed.scheme == "https" ? "tls" : "invalid-url";
        session->result->errorKind = kind;
        session->result->errorMessage = parsed.error;
        ScheduleNotify(session);
        return;
    }
    auto request = std::make_shared<Request>(shared_from_this(), session);
    request->url = std::move(parsed);
    request->selfGuard = request;
    requests_.push_back(request);
    request->Begin();
}

void HttpManager::NotifySession(const std::shared_ptr<Session>& session)
{
    if (session->notified || session->silent)
        return;
    session->notified = true;
    if (session->onDone)
        session->onDone();
}

void HttpManager::ScheduleNotify(const std::shared_ptr<Session>& session)
{
    if (!session->onDone)
        return;
    Lode::State vm(session->mainL);
    std::function<void()> done = session->onDone;
    Lode::Value fn = vm.CreateFunction([this, session](Lode::State&, const std::vector<Lode::Value>&) -> Lode::Value {
        NotifySession(session);
        return Lode::Value();
    });
    Lode::Task::Defer(vm, fn);
}

void HttpManager::Remove(const std::shared_ptr<Request>& request)
{
    auto it = std::find(requests_.begin(), requests_.end(), request);
    if (it != requests_.end())
        requests_.erase(it);
}

void HttpManager::AbortAll()
{
    std::vector<std::shared_ptr<Request>> snapshot = requests_;
    for (auto& request : snapshot)
    {
        if (!request->requestComplete)
            request->FinishError("aborted", "request aborted");
    }
}

void HttpManager::Shutdown()
{
    std::vector<std::shared_ptr<Request>> snapshot = std::move(requests_);
    requests_.clear();
    for (auto& request : snapshot)
    {
        request->session->silent = true;
        request->CloseHandles();
    }
}

} // namespace lodehttp
