// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Coroutine.hpp"
#include "Lode/Module.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/ObjectWrap.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Task.hpp"
#include "Lode/Value.hpp"
#include "Lode/EventLoop.hpp"

#include "uv.h"
#include "base64.hpp"
#include "sha1.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct WebSocketManager;
struct WsClient;
struct WsServer;

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<WsClient>& client, const Lode::Table& methods);
Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<WsServer>& server, const Lode::Table& methods);

// ---------------------------------------------------------------------------
// Constants (RFC 6455)
// ---------------------------------------------------------------------------

constexpr uint8_t kOpcodeContinuation = 0x0;
constexpr uint8_t kOpcodeText = 0x1;
constexpr uint8_t kOpcodeBinary = 0x2;
constexpr uint8_t kOpcodeClose = 0x8;
constexpr uint8_t kOpcodePing = 0x9;
constexpr uint8_t kOpcodePong = 0xA;

constexpr uint16_t kCloseNormal = 1000;
constexpr uint16_t kCloseProtocolError = 1002;
constexpr uint16_t kCloseAbnormal = 1006;

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum class WsState
{
    Connecting,
    Open,
    Closing,
    Closed,
};

enum class TimerMode
{
    None,
    Connect,
    Close,
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string FormatIpAddress(const struct sockaddr* addr)
{
    char host[64] = {0};
    if (addr->sa_family == AF_INET)
    {
        uv_ip4_name(reinterpret_cast<const struct sockaddr_in*>(addr), host, sizeof(host));
    }
    else if (addr->sa_family == AF_INET6)
    {
        uv_ip6_name(reinterpret_cast<const struct sockaddr_in6*>(addr), host, sizeof(host));
    }
    else
    {
        return "?";
    }
    return std::string(host);
}

std::string FormatSockAddr(const struct sockaddr* addr, int& portOut)
{
    portOut = 0;
    char host[64] = {0};
    if (addr->sa_family == AF_INET)
    {
        const auto* a = reinterpret_cast<const struct sockaddr_in*>(addr);
        uv_ip4_name(a, host, sizeof(host));
        portOut = ntohs(a->sin_port);
    }
    else if (addr->sa_family == AF_INET6)
    {
        const auto* a = reinterpret_cast<const struct sockaddr_in6*>(addr);
        uv_ip6_name(a, host, sizeof(host));
        portOut = ntohs(a->sin6_port);
    }
    else
    {
        return "?";
    }
    return std::string(host);
}

bool IsValidPort(double value, bool allowZero = false)
{
    double minPort = allowZero ? 0.0 : 1.0;
    return std::isfinite(value) && std::trunc(value) == value && value >= minPort && value <= 65535.0;
}

bool IsValidCloseCode(double value)
{
    if (!std::isfinite(value) || std::trunc(value) != value || value < 1000.0 || value > 4999.0)
        return false;
    int code = static_cast<int>(value);
    return code != 1004 && code != 1005 && code != 1006 && code != 1015;
}

// --- small PRNG for masking keys and handshake nonces ---

struct WsRng
{
    uint32_t state = 0;

    void Seed(uint64_t seed)
    {
        state = (static_cast<uint32_t>(seed) ^ 0x9E3779B9u) | 1u;
    }

    uint32_t Next()
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    void Fill(uint8_t* dst, size_t n)
    {
        size_t i = 0;
        while (i + 4 <= n)
        {
            uint32_t v = Next();
            dst[i + 0] = static_cast<uint8_t>(v & 0xFF);
            dst[i + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            dst[i + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            dst[i + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
            i += 4;
        }
        if (i < n)
        {
            uint32_t v = Next();
            while (i < n)
            {
                dst[i] = static_cast<uint8_t>(v & 0xFF);
                v >>= 8;
                ++i;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Parsed URL
// ---------------------------------------------------------------------------

struct ParsedWsUrl
{
    bool valid = false;
    std::string error;
    std::string host;        // display host (with brackets for IPv6)
    std::string connectHost; // host for DNS (no brackets)
    int port = 0;
    std::string path;        // includes query, always starts with '/'
};

ParsedWsUrl ParseWebSocketUrl(const std::string& raw)
{
    ParsedWsUrl out;

    if (raw.size() < 5 || raw.compare(0, 5, "ws://") != 0 && raw.compare(0, 5, "WS://") != 0)
    {
        if (raw.size() >= 6 && (raw.compare(0, 6, "wss://") == 0 || raw.compare(0, 6, "WSS://") == 0))
            out.error = "tls";
        else
            out.error = "invalid-url";
        return out;
    }

    std::string rest = raw.substr(5);
    if (rest.empty())
    {
        out.error = "invalid-url: missing host";
        return out;
    }

    size_t pathStart = rest.find('/');
    size_t queryStart = rest.find('?');
    size_t authorityEnd = rest.size();
    if (pathStart != std::string::npos && queryStart != std::string::npos)
        authorityEnd = (std::min)(pathStart, queryStart);
    else if (pathStart != std::string::npos)
        authorityEnd = pathStart;
    else if (queryStart != std::string::npos)
        authorityEnd = queryStart;

    std::string authority = rest.substr(0, authorityEnd);
    std::string path = rest.substr(authorityEnd);
    if (path.empty() || path[0] != '/')
        path = "/" + path;

    if (authority.empty())
    {
        out.error = "invalid-url: missing host";
        return out;
    }

    int port = 80;
    std::string host;
    if (!authority.empty() && authority[0] == '[')
    {
        size_t close = authority.find(']');
        if (close == std::string::npos)
        {
            out.error = "invalid-url: malformed IPv6 host";
            return out;
        }
        host = authority.substr(0, close + 1);
        std::string portPart = authority.substr(close + 1);
        if (!portPart.empty())
        {
            if (portPart[0] != ':')
            {
                out.error = "invalid-url: malformed host";
                return out;
            }
            try
            {
                port = std::stoi(portPart.substr(1));
            }
            catch (...)
            {
                out.error = "invalid-url: malformed port";
                return out;
            }
        }
    }
    else
    {
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos)
        {
            host = authority.substr(0, colon);
            std::string portStr = authority.substr(colon + 1);
            if (portStr.empty() || !std::all_of(portStr.begin(), portStr.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            {
                out.error = "invalid-url: malformed port";
                return out;
            }
            try
            {
                port = std::stoi(portStr);
            }
            catch (...)
            {
                out.error = "invalid-url: malformed port";
                return out;
            }
        }
        else
        {
            host = authority;
        }
    }

    if (host.empty() || port < 1 || port > 65535)
    {
        out.error = "invalid-url: malformed host or port";
        return out;
    }

    out.valid = true;
    out.host = host;
    out.port = port;
    out.path = path;
    out.connectHost = host;
    if (!host.empty() && host[0] == '[' && host.back() == ']')
        out.connectHost = host.substr(1, host.size() - 2);
    return out;
}

// ---------------------------------------------------------------------------
// HTTP header parsing
// ---------------------------------------------------------------------------

struct ParsedHeaders
{
    std::string statusLine;
    std::vector<std::pair<std::string, std::string>> fields;

    const std::string* Find(const std::string& name) const
    {
        for (const auto& kv : fields)
        {
            if (EqualsIgnoreCase(kv.first, name))
                return &kv.second;
        }
        return nullptr;
    }

    static bool EqualsIgnoreCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }
};

// Parses "status-line / headers\r\n\r\n" (the block must include the trailing
// blank line). Returns false if the block is structurally invalid.
bool ParseHeaderBlock(const std::string& block, ParsedHeaders& out)
{
    size_t lineStart = 0;
    size_t pos = block.find("\r\n");
    if (pos == std::string::npos)
        return false;
    out.statusLine = block.substr(0, pos);
    lineStart = pos + 2;

    while (lineStart < block.size())
    {
        size_t nl = block.find("\r\n", lineStart);
        if (nl == std::string::npos)
            return false;
        std::string line = block.substr(lineStart, nl - lineStart);
        if (line.empty())
            break;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return false;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        size_t first = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t");
        if (first != std::string::npos && last != std::string::npos)
            value = value.substr(first, last - first + 1);
        else
            value.clear();
        out.fields.emplace_back(name, value);
        lineStart = nl + 2;
    }
    return true;
}

bool HeaderListContains(const std::string* value, const std::string& token)
{
    if (!value)
        return false;
    size_t start = 0;
    while (start <= value->size())
    {
        size_t comma = value->find(',', start);
        std::string part = comma == std::string::npos ? value->substr(start) : value->substr(start, comma - start);
        size_t first = part.find_first_not_of(" \t");
        size_t last = part.find_last_not_of(" \t");
        if (first == std::string::npos)
        {
            if (comma == std::string::npos)
                break;
            start = comma + 1;
            continue;
        }
        std::string trimmed = part.substr(first, last - first + 1);
        if (ParsedHeaders::EqualsIgnoreCase(trimmed, token))
            return true;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Frame encode
// ---------------------------------------------------------------------------

std::vector<char> EncodeFrame(uint8_t opcode, const std::vector<char>& payload, bool fin, bool masked, uint32_t maskKey)
{
    std::vector<char> out;
    size_t len = payload.size();
    out.reserve(len + 14);

    out.push_back(static_cast<char>((fin ? 0x80 : 0) | (opcode & 0x0F)));

    char b1 = static_cast<char>(masked ? 0x80 : 0);
    if (len < 126)
    {
        out.push_back(static_cast<char>(b1 | static_cast<char>(len)));
    }
    else if (len <= 0xFFFF)
    {
        out.push_back(static_cast<char>(b1 | 126));
        out.push_back(static_cast<char>((len >> 8) & 0xFF));
        out.push_back(static_cast<char>(len & 0xFF));
    }
    else
    {
        out.push_back(static_cast<char>(b1 | 127));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
    }

    if (masked)
    {
        out.push_back(static_cast<char>((maskKey >> 0) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 8) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 16) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 24) & 0xFF));
        for (size_t i = 0; i < len; ++i)
        {
            uint8_t keyByte = static_cast<uint8_t>((maskKey >> (8 * (i % 4))) & 0xFF);
            out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ keyByte));
        }
    }
    else
    {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

std::vector<char> MakeClosePayload(uint16_t code, const std::string& reason)
{
    std::vector<char> payload;
    payload.reserve(reason.size() + 2);
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    for (size_t i = 0; i < reason.size() && payload.size() < 125; ++i)
        payload.push_back(reason[i]);
    return payload;
}

std::string ComputeAcceptKey(const std::string& clientKey)
{
    auto digest = ws::Sha1(clientKey + kWebSocketGuid);
    return ws::Base64Encode(digest.data(), digest.size());
}

// ---------------------------------------------------------------------------
// WebSocketManager
// ---------------------------------------------------------------------------

struct WebSocketManager : std::enable_shared_from_this<WebSocketManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table clientMethods;
    Lode::Table serverMethods;

    std::vector<std::shared_ptr<WsClient>> clients;
    std::vector<std::shared_ptr<WsServer>> servers;

    WsRng rng;

    void AddClient(const std::shared_ptr<WsClient>& client) { clients.push_back(client); }

    void AddServer(const std::shared_ptr<WsServer>& server) { servers.push_back(server); }

    void RemoveClient(const std::shared_ptr<WsClient>& client)
    {
        auto it = std::find(clients.begin(), clients.end(), client);
        if (it != clients.end())
            clients.erase(it);
    }

    void RemoveServer(const std::shared_ptr<WsServer>& server)
    {
        auto it = std::find(servers.begin(), servers.end(), server);
        if (it != servers.end())
            servers.erase(it);
    }

    void Shutdown();
};

// ---------------------------------------------------------------------------
// WsClient
// ---------------------------------------------------------------------------

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

struct WsClient : std::enable_shared_from_this<WsClient>
{
    std::shared_ptr<WebSocketManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_tcp_t tcp{};
    uv_connect_t connReq{};
    uv_getaddrinfo_t addrReq{};
    uv_timer_t timer{};
    bool tcpInited = false;
    bool addrInited = false;
    bool timerInited = false;
    bool tcpClosed = false;
    bool timerClosed = false;

    bool serverSide = false;
    WsState state = WsState::Connecting;
    bool opened = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    bool disconnectedFired = false;
    bool connectPending = false;
    bool connectResumed = false;
    int closeCount = 0;

    Lode::Coroutine connectCo;
    ParsedWsUrl parsedUrl;
    uint64_t connectTimeoutMs = 0;
    std::vector<std::pair<std::string, std::string>> requestHeaders;

    std::string remoteHost;
    int remotePort = 0;
    std::string localHost;
    int localPort = 0;

    // Handshake
    std::vector<char> recvBuf;
    bool handshakeComplete = false;
    std::string sentKey;       // client side
    Lode::Value wrappedValue;
    std::shared_ptr<WsServer> ownerServer; // server side

    // Frames
    uint8_t fragmentOpcode = 0;
    bool fragmentInProgress = false;
    std::vector<char> fragmentBuf;

    // Close
    bool closeSent = false;
    bool closeReceived = false;
    int closeCode = 0;
    std::string closeReason;
    bool hasCloseInfo = false;

    // Events
    std::shared_ptr<Lode::Signal> connectedSig;
    std::shared_ptr<Lode::Signal> messageSig;
    std::shared_ptr<Lode::Signal> disconnectedSig;
    std::shared_ptr<Lode::Signal> errorSig;

    Lode::Value connectedProxy;
    Lode::Value messageProxy;
    Lode::Value disconnectedProxy;
    Lode::Value errorProxy;

    std::shared_ptr<WsClient> selfGuard;

    void InitSignals(Lode::State& vm)
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

    void FireError(const std::string& message)
    {
        if (mgr->shuttingDown || closed || closing)
            return;
        errorSig->Fire(Lode::Value(message));
    }

    void UpdateAddresses()
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

    // --- coroutine helpers ---

    void NotifyConnectOk()
    {
        if (!connectCo.IsValid())
            return;
        Lode::State vm(mainL);
        auto res = connectCo.Resume({});
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }

    void NotifyConnectError(const std::string& message)
    {
        if (!connectCo.IsValid())
            return;
        Lode::State vm(mainL);
        auto res = connectCo.ResumeError(message);
        if (res.IsError() && Lode::Task::IsMainThread(vm, connectCo.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, res.GetError().ErrorMessage());
        connectCo = Lode::Coroutine();
    }

    // --- timers ---

    TimerMode timerMode = TimerMode::None;

    void StartTimer(uint64_t ms, TimerMode mode)
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

    void StopTimer()
    {
        if (timerInited && !timerClosed)
            uv_timer_stop(&timer);
    }

    static void OnTimer(uv_timer_t* timer)
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

    // --- connect (client side) ---

    void StartTcpConnect(const struct sockaddr* addr)
    {
        std::memset(&tcp, 0, sizeof(tcp));
        tcpInited = true;
        tcp.data = this;
        int r = uv_tcp_init(loop, &tcp);
        if (r != 0)
        {
            FailConnect(std::string("websocket Connect: tcp: ") + uv_strerror(r));
            return;
        }
        std::memset(&connReq, 0, sizeof(connReq));
        connReq.data = this;
        r = uv_tcp_connect(&connReq, &tcp, addr, OnConnected);
        if (r != 0)
        {
            FailConnect(std::string("websocket Connect: connect: ") + uv_strerror(r));
            return;
        }
    }

    int BeginConnect()
    {
        std::memset(&addrReq, 0, sizeof(addrReq));
        addrInited = true;
        addrReq.data = this;
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        std::string portStr = std::to_string(parsedUrl.port);
        int r = uv_getaddrinfo(loop, &addrReq, OnResolved, parsedUrl.connectHost.c_str(), portStr.c_str(), &hints);
        if (r != 0)
        {
            addrInited = false;
            return r;
        }
        if (connectTimeoutMs > 0)
            StartTimer(connectTimeoutMs, TimerMode::Connect);
        return 0;
    }

    Lode::Value MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args)
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
            std::string kind = parsed.error == "tls" ? "tls" : "invalid-url";
            vm.RaiseError("websocket Connect: " + kind + (parsed.error == "tls" ? ": wss:// is not supported yet" : ": invalid URL"));
            return Lode::Value();
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

        int r = BeginConnect();
        if (r != 0)
        {
            connectPending = false;
            connectCo = Lode::Coroutine();
            vm.RaiseError("websocket Connect: " + std::string(uv_strerror(r)));
            return Lode::Value();
        }
        return vm.YieldThread();
    }

    // --- send / frames ---

    bool SendBytes(std::vector<char> data)
    {
        if (closed || closing || !tcpInited || tcpClosed)
            return false;
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
            return false;
        }
        return true;
    }

    bool SendFrame(uint8_t opcode, const std::vector<char>& payload, bool fin = true)
    {
        uint32_t maskKey = serverSide ? 0u : mgr->rng.Next();
        std::vector<char> frame = EncodeFrame(opcode, payload, fin, !serverSide, maskKey);
        return SendBytes(std::move(frame));
    }

    bool SendDataFrame(uint8_t opcode, const std::vector<char>& payload)
    {
        if (state != WsState::Open)
            return false;
        return SendFrame(opcode, payload, true);
    }

    void SendPong(const std::vector<char>& payload)
    {
        SendFrame(kOpcodePong, payload, true);
    }

    void SendCloseFrame(uint16_t code, const std::string& reason)
    {
        std::vector<char> payload = MakeClosePayload(code, reason);
        SendFrame(kOpcodeClose, payload, true);
    }

    Lode::Value MethodSend(Lode::State& vm, const std::vector<Lode::Value>& args)
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
        if (!SendDataFrame(opcode, data))
        {
            vm.RaiseError("websocket Send: failed to queue write");
            return Lode::Value();
        }
        return Lode::Value();
    }

    Lode::Value MethodPing(Lode::State& vm, const std::vector<Lode::Value>& args)
    {
        if (state != WsState::Open)
        {
            vm.RaiseError("websocket Ping: not connected");
            return Lode::Value();
        }
        std::vector<char> payload;
        if (args.size() > 1 && !args[1].IsNil())
        {
            if (!args[1].IsString() && !args[1].IsBuffer())
            {
                vm.RaiseError("websocket Ping: data must be a string, buffer, or nil");
                return Lode::Value();
            }
            if (args[1].IsString())
            {
                const std::string& text = args[1].AsString();
                payload.assign(text.begin(), text.end());
            }
            else
            {
                size_t size = 0;
                void* ptr = args[1].AsBuffer(&size);
                if (ptr && size > 0)
                    payload.assign(static_cast<const char*>(ptr), static_cast<const char*>(ptr) + size);
            }
            if (payload.size() > 125)
            {
                vm.RaiseError("websocket Ping: payload must be at most 125 bytes");
                return Lode::Value();
            }
        }
        SendFrame(kOpcodePing, payload, true);
        return Lode::Value();
    }

    Lode::Value MethodClose(Lode::State& vm, const std::vector<Lode::Value>& args)
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
            SendCloseFrame(code, reason);
            state = WsState::Closing;
            StartTimer(5000, TimerMode::Close);
        }
        else if (state == WsState::Connecting)
        {
            // Abort the in-progress connect; RequestClose notifies the coroutine.
            RequestClose();
        }
        return Lode::Value();
    }

    // --- incoming data processing ---

    void ProcessIncoming()
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
            if (!HandleHandshake(headerBlock))
                return;
            if (closed || closing)
                return;
        }
        ParseFrames();
    }

    bool HandleHandshake(const std::string& headerBlock)
    {
        ParsedHeaders headers;
        if (!ParseHeaderBlock(headerBlock, headers))
        {
            HandshakeFail("malformed handshake");
            return false;
        }

        if (serverSide)
        {
            // Server side: expect GET /path HTTP/1.1 with upgrade headers.
            if (headers.statusLine.compare(0, 4, "GET ") != 0 ||
                headers.statusLine.find(" HTTP/1.1") == std::string::npos)
            {
                SendHttpResponse("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                HandshakeFail("invalid websocket handshake request");
                return false;
            }
            const std::string* upgrade = headers.Find("Upgrade");
            const std::string* connection = headers.Find("Connection");
            const std::string* key = headers.Find("Sec-WebSocket-Key");
            const std::string* version = headers.Find("Sec-WebSocket-Version");
            if (!upgrade || !ParsedHeaders::EqualsIgnoreCase(*upgrade, "websocket") ||
                !HeaderListContains(connection, "Upgrade") || !key || !version || *version != "13")
            {
                SendHttpResponse("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                HandshakeFail("invalid websocket handshake request");
                return false;
            }
            std::string accept = ComputeAcceptKey(*key);
            std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                   "Upgrade: websocket\r\n"
                                   "Connection: Upgrade\r\n"
                                   "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
            SendBytes(std::vector<char>(response.begin(), response.end()));
            CompleteHandshake();
            return true;
        }

        // Client side: expect 101 with matching Sec-WebSocket-Accept.
        if (headers.statusLine.find(" 101 ") == std::string::npos)
        {
            HandshakeFail("websocket handshake rejected (expected 101)");
            return false;
        }
        const std::string* upgrade = headers.Find("Upgrade");
        const std::string* connection = headers.Find("Connection");
        const std::string* accept = headers.Find("Sec-WebSocket-Accept");
        if (!upgrade || !ParsedHeaders::EqualsIgnoreCase(*upgrade, "websocket") ||
            !HeaderListContains(connection, "Upgrade") || !accept || *accept != ComputeAcceptKey(sentKey))
        {
            HandshakeFail("websocket handshake rejected (bad accept key)");
            return false;
        }
        CompleteHandshake();
        return true;
    }

    void HandshakeFail(const std::string& message)
    {
        if (serverSide)
        {
            if (!closed && !closing)
                FireError(message);
            RequestClose();
        }
        else
        {
            FailConnect(message);
        }
    }

    void CompleteHandshake()
    {
        handshakeComplete = true;
        StopTimer();
        opened = true;
        state = WsState::Open;
        if (serverSide)
        {
            if (!mgr->shuttingDown && wrappedValue.IsUserdata())
                FireServerClientConnected();
        }
        else
        {
            if (!mgr->shuttingDown)
                connectedSig->Fire();
            if (!connectResumed && connectCo.IsValid())
            {
                connectResumed = true;
                NotifyConnectOk();
            }
        }
    }

    void StartReading()
    {
        if (reading || closed || closing)
            return;
        reading = true;
        int r = uv_read_start(reinterpret_cast<uv_stream_t*>(&tcp), AllocBuffer, OnRead);
        if (r != 0)
        {
            reading = false;
            FireError(std::string("websocket read: ") + uv_strerror(r));
            RequestClose();
        }
    }

    // Fires the owning server's ClientConnected signal. Defined after WsServer.
    void FireServerClientConnected();

    void ParseFrames()
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
                ProtocolError("reserved bits set");
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
                    ProtocolError("server frame was masked");
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
                ProtocolError("client frame was not masked");
                return;
            }

            if (!HandleFrame(fin, opcode, payload))
                return;
        }
    }

    // Returns false if the connection should stop being processed.
    bool HandleFrame(bool fin, uint8_t opcode, std::vector<char>& payload)
    {
        if (opcode == kOpcodePing || opcode == kOpcodePong || opcode == kOpcodeClose)
        {
            if (!fin || payload.size() > 125)
            {
                ProtocolError("invalid control frame");
                return false;
            }
            if (opcode == kOpcodePing)
            {
                SendPong(payload);
                return true;
            }
            if (opcode == kOpcodePong)
                return true;
            // Close frame
            HandleCloseFrame(payload);
            return false;
        }

        if (opcode == kOpcodeContinuation)
        {
            if (!fragmentInProgress)
            {
                ProtocolError("unexpected continuation frame");
                return false;
            }
            fragmentBuf.insert(fragmentBuf.end(), payload.begin(), payload.end());
            if (fin)
            {
                std::vector<char> message = std::move(fragmentBuf);
                uint8_t messageOpcode = fragmentOpcode;
                fragmentBuf.clear();
                fragmentInProgress = false;
                fragmentOpcode = 0;
                DeliverMessage(messageOpcode, message);
            }
            return true;
        }

        if (opcode != kOpcodeText && opcode != kOpcodeBinary)
        {
            ProtocolError("unknown opcode");
            return false;
        }
        if (fragmentInProgress)
        {
            ProtocolError("new data frame while fragmented");
            return false;
        }
        if (!fin)
        {
            fragmentOpcode = opcode;
            fragmentInProgress = true;
            fragmentBuf = std::move(payload);
            return true;
        }
        DeliverMessage(opcode, payload);
        return true;
    }

    void DeliverMessage(uint8_t opcode, const std::vector<char>& payload)
    {
        (void)opcode;
        if (mgr->shuttingDown)
            return;
        std::string text(payload.begin(), payload.end());
        messageSig->Fire(Lode::Value(text));
    }

    void HandleCloseFrame(const std::vector<char>& payload)
    {
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
            SendCloseFrame(static_cast<uint16_t>(closeCode == kCloseAbnormal ? kCloseNormal : closeCode), "");
        }
        if (state == WsState::Open)
            state = WsState::Closing;
        RequestClose();
    }

    void ProtocolError(const std::string& message)
    {
        FireError("websocket: protocol error: " + message);
        if (!closeSent)
        {
            closeSent = true;
            SendCloseFrame(kCloseProtocolError, "");
        }
        RequestClose();
    }

    // --- close / lifecycle ---

    void CloseHandles()
    {
        if (closed)
            return;
        connectPending = false;
        if (reading)
        {
            uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp));
            reading = false;
        }
        StopTimer();
        if (timerInited && !timerClosed)
        {
            timerClosed = true;
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

    void RequestClose()
    {
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

    void FailConnect(const std::string& message)
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

    void CheckClosed()
    {
        if (!closing || closeCount != 0 || addrInited)
            return;
        FinishClosed();
    }

    void FinishClosed()
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

    // --- static uv callbacks ---

    static void OnHandleClosed(uv_handle_t* handle)
    {
        auto* self = static_cast<WsClient*>(handle->data);
        self->closeCount -= 1;
        self->CheckClosed();
    }

    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res)
    {
        auto* self = static_cast<WsClient*>(req->data);
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
            self->FailConnect(std::string("websocket Connect: dns: ") + uv_strerror(status));
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

    void SendHandshakeRequest()
    {
        uint8_t nonce[16];
        mgr->rng.Fill(nonce, sizeof(nonce));
        sentKey = ws::Base64Encode(nonce, sizeof(nonce));

        std::string hostHeader = parsedUrl.host;
        if (parsedUrl.port != 80)
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
        SendBytes(std::vector<char>(request.begin(), request.end()));
    }

    static void OnConnected(uv_connect_t* req, int status)
    {
        auto* self = static_cast<WsClient*>(req->data);
        if (self->closing)
            return;
        if (status != 0)
        {
            self->FailConnect(std::string("websocket Connect: connect: ") + uv_strerror(status));
            return;
        }
        uv_tcp_nodelay(&self->tcp, 1);
        self->UpdateAddresses();
        self->SendHandshakeRequest();
        self->StartReading();
    }

    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
    {
        (void)handle;
        (void)suggestedSize;
        static constexpr size_t kChunkSize = 64 * 1024;
        buf->base = new char[kChunkSize];
        buf->len = kChunkSize;
    }

    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
    {
        auto* self = static_cast<WsClient*>(stream->data);
        if (nread > 0)
        {
            self->recvBuf.insert(self->recvBuf.end(), buf->base, buf->base + nread);
            self->ProcessIncoming();
        }
        else if (nread == UV_EOF)
        {
            self->OnEof();
        }
        else if (nread < 0)
        {
            if (nread != UV_ECANCELED)
            {
                self->FireError(std::string("websocket read: ") + uv_strerror(static_cast<int>(nread)));
                self->RequestClose();
            }
        }
        delete[] buf->base;
    }

    void OnEof()
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

    static void OnWritten(uv_write_t* req, int status)
    {
        auto* wreq = reinterpret_cast<WriteRequest*>(req);
        auto* self = static_cast<WsClient*>(req->handle->data);
        if (status != 0 && !self->closing && !self->closed)
            self->FireError(std::string("websocket write: ") + uv_strerror(status));
        delete wreq;
    }

    // Server-side helpers
    void SendHttpResponse(const std::string& response)
    {
        SendBytes(std::vector<char>(response.begin(), response.end()));
    }
};

// ---------------------------------------------------------------------------
// WsServer
// ---------------------------------------------------------------------------

struct WsServer : std::enable_shared_from_this<WsServer>
{
    std::shared_ptr<WebSocketManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_tcp_t tcp{};
    bool tcpInited = false;
    bool tcpClosed = false;
    bool listening = false;
    bool closing = false;
    bool closed = false;
    int backlog = 511;

    std::string localHost;
    int localPort = 0;

    std::shared_ptr<Lode::Signal> clientSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value clientProxy;
    Lode::Value errorProxy;

    std::shared_ptr<WsServer> selfGuard;

    void InitSignals(Lode::State& vm)
    {
        clientSig = Lode::Signal::Create(vm);
        errorSig = Lode::Signal::Create(vm);
        clientProxy = clientSig->CreatePublic();
        errorProxy = errorSig->CreatePublic();
    }

    void FireError(const std::string& message)
    {
        if (mgr->shuttingDown || closed || closing)
            return;
        errorSig->Fire(Lode::Value(message));
    }

    void UpdateAddresses()
    {
        struct sockaddr_storage addr;
        int namelen = static_cast<int>(sizeof(addr));
        if (uv_tcp_getsockname(&tcp, reinterpret_cast<struct sockaddr*>(&addr), &namelen) == 0)
        {
            localHost = FormatSockAddr(reinterpret_cast<const struct sockaddr*>(&addr), localPort);
        }
    }

    Lode::Value MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args)
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
        if (args.size() < 1 || !args[1].IsNumber())
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

    Lode::Value MethodLocalAddress(Lode::State& vm)
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

    void BindFail(Lode::State& vm, const std::string& message)
    {
        if (tcpInited && !tcpClosed)
        {
            tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp), OnHandleClosed);
        }
        vm.RaiseError("websocket Server: " + message);
    }

    void RequestClose()
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

    void FinishClosed()
    {
        if (closed)
            return;
        closed = true;
        listening = false;
        mgr->RemoveServer(shared_from_this());
        selfGuard.reset();
    }

    static int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
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

    // --- static uv callbacks ---

    static void OnHandleClosed(uv_handle_t* handle)
    {
        auto* self = static_cast<WsServer*>(handle->data);
        self->FinishClosed();
    }

    static void OnConnection(uv_stream_t* server, int status)
    {
        auto* self = static_cast<WsServer*>(server->data);
        if (self->closing || self->closed)
            return;
        if (status != 0)
        {
            self->FireError(std::string("accept: ") + uv_strerror(status));
            return;
        }
        Lode::State vm(self->mainL);
        auto client = std::make_shared<WsClient>();
        client->mgr = self->mgr;
        client->mainL = self->mainL;
        client->loop = self->loop;
        client->serverSide = true;
        client->tcpInited = true;
        std::memset(&client->tcp, 0, sizeof(client->tcp));
        client->tcp.data = client.get();
        int r = uv_tcp_init(self->loop, &client->tcp);
        if (r != 0)
        {
            client->tcpClosed = true;
            uv_close(reinterpret_cast<uv_handle_t*>(&client->tcp), WsClient::OnHandleClosed);
            self->FireError(std::string("tcp init: ") + uv_strerror(r));
            return;
        }
        client->InitSignals(vm);
        client->ownerServer = self->shared_from_this();
        self->mgr->AddClient(client);
        client->selfGuard = client;
        r = uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->tcp));
        if (r != 0)
        {
            client->RequestClose();
            self->FireError(std::string("accept: ") + uv_strerror(r));
            return;
        }
        uv_tcp_nodelay(&client->tcp, 1);
        client->UpdateAddresses();

        Lode::Value clientValue = WrapClient(vm, client, self->mgr->clientMethods);
        client->wrappedValue = clientValue;
        client->StartReading();
    }
};

// ---------------------------------------------------------------------------
// WebSocketManager Implementation
// ---------------------------------------------------------------------------

void WsClient::FireServerClientConnected()
{
    if (!ownerServer || ownerServer->closing || ownerServer->closed)
        return;
    ownerServer->clientSig->Fire(wrappedValue);
}

void WebSocketManager::Shutdown()
{
    shuttingDown = true;
    auto clientsCopy = clients;
    for (auto& client : clientsCopy)
        client->RequestClose();
    auto serversCopy = servers;
    for (auto& server : serversCopy)
        server->RequestClose();
}

// ---------------------------------------------------------------------------
// Class wrapping (metatables + __index)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Method tables
// ---------------------------------------------------------------------------

Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<WebSocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Connect", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Connect: invalid WebSocket");
            return Lode::Value();
        }
        if (self->serverSide)
        {
            vm2.RaiseError("websocket Connect: cannot call Connect on a server-side WebSocket");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket Connect: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodConnect(vm2, args);
    }));

    m.Set("Send", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Send: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodSend(vm2, args);
    }));

    m.Set("Ping", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Ping: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodPing(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Close: invalid WebSocket");
            return Lode::Value();
        }
        return self->MethodClose(vm2, args);
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Destroy: invalid WebSocket");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsConnected", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->state == WsState::Open && !self->closed);
    }));

    m.Set("ReadyState", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsClient>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(std::string("closed"));
        switch (self->state)
        {
            case WsState::Connecting: return Lode::Value(std::string("connecting"));
            case WsState::Open:       return Lode::Value(std::string("open"));
            case WsState::Closing:    return Lode::Value(std::string("closing"));
            case WsState::Closed:     return Lode::Value(std::string("closed"));
        }
        return Lode::Value(std::string("closed"));
    }));

    return m;
}

Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<WebSocketManager>& mgr)
{
    Lode::Table m = vm.CreateTable();

    m.Set("Listen", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket Server: runtime is shutting down");
            return Lode::Value();
        }
        return self->MethodListen(vm2, args);
    }));

    m.Set("Close", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("Destroy", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        self->RequestClose();
        return Lode::Value();
    }));

    m.Set("IsListening", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
            return Lode::Value(false);
        return Lode::Value(self->listening && !self->closing && !self->closed);
    }));

    m.Set("LocalAddress", vm.CreateFunction([](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        auto self = Lode::ObjectWrap<WsServer>::Unwrap(vm2, 1);
        if (!self)
        {
            vm2.RaiseError("websocket Server: invalid Server");
            return Lode::Value();
        }
        return self->MethodLocalAddress(vm2);
    }));

    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Module entry
// ---------------------------------------------------------------------------

LODE_MODULE(vm)
{
    auto mgr = std::make_shared<WebSocketManager>();
    mgr->mainL = vm.GetMainThread();
    mgr->loop = vm.GetEventLoop().GetUVLoop();
    mgr->rng.Seed(uv_hrtime());
    Lode::Task::RegisterShutdownHook(vm, [mgr]() { mgr->Shutdown(); });

    mgr->clientMethods = BuildClientMethods(vm, mgr);
    mgr->serverMethods = BuildServerMethods(vm, mgr);

    Lode::Table wsClass = vm.CreateTable();
    wsClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>&) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket: runtime is shutting down");
            return Lode::Value();
        }
        auto client = std::make_shared<WsClient>();
        client->mgr = mgr;
        client->mainL = mgr->mainL;
        client->loop = mgr->loop;
        client->InitSignals(vm2);
        mgr->AddClient(client);
        client->selfGuard = client;
        return WrapClient(vm2, client, mgr->clientMethods);
    }));

    Lode::Table serverClass = vm.CreateTable();
    serverClass.Set("Create", vm.CreateFunction([mgr](Lode::State& vm2, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (mgr->shuttingDown)
        {
            vm2.RaiseError("websocket: runtime is shutting down");
            return Lode::Value();
        }
        auto server = std::make_shared<WsServer>();
        server->mgr = mgr;
        server->mainL = mgr->mainL;
        server->loop = mgr->loop;
        if (args.size() > 1 && !args[1].IsNil())
        {
            if (!args[1].IsTable())
            {
                vm2.RaiseError("websocket.WebSocketServer:Create: opts must be a table or nil");
                return Lode::Value();
            }
            auto backlog = args[1].AsTable().Get("backlog");
            if (backlog.IsOk() && !backlog.GetValue().IsNil())
            {
                double value = backlog.GetValue().AsNumber();
                if (!IsValidPort(value))
                {
                    vm2.RaiseError("websocket.WebSocketServer:Create: backlog must be an integer between 1 and 65535");
                    return Lode::Value();
                }
                server->backlog = static_cast<int>(value);
            }
        }
        server->InitSignals(vm2);
        mgr->AddServer(server);
        server->selfGuard = server;
        return WrapServer(vm2, server, mgr->serverMethods);
    }));

    Lode::Exports exports(vm);

    exports.SetTable("WebSocket", wsClass);
    exports.SetTable("WebSocketServer", serverClass);

    return Lode::ModuleReturn(exports.GetExportTable());
}
