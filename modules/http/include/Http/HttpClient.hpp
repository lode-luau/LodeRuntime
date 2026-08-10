// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Http/HttpManager.hpp"
#include "Http/HttpHelpers.hpp"
#include "Http/HttpTypes.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#ifdef _WIN32
#include "Http/HttpTls.hpp"
#endif
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lodehttp
{

struct HttpResponseData
{
    int status = 0;
    std::string statusText;
    std::string version;
    std::vector<HeaderPair> headers;
    std::string body;
    std::string finalUrl;
    std::string errorKind;
    std::string errorMessage;
};

struct HttpClient;

struct HttpRequestContext : std::enable_shared_from_this<HttpRequestContext>
{
    std::shared_ptr<HttpClient> client;
    ParsedUrl url;
    HttpRequestOptions opts;
    std::shared_ptr<HttpResponseData> result;
    
    uv_loop_t* loop = nullptr;
    uv_tcp_t tcp{};
    uv_connect_t connReq{};
    uv_getaddrinfo_t addrReq{};
    uv_timer_t timer{};
    uv_write_t writeReq{};
    uv_buf_t writeBuf{};

    bool addrInited = false;
    bool dnsDone = false;
    bool tcpInited = false;
    bool timerInited = false;
    bool tcpClosed = false;
    bool timerClosed = false;
    bool closed = false;
    bool reading = false;
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
    int64_t redirectsDone = 0;

    Lode::Coroutine taskCtx;
    bool isAsync = false;

    // TLS (HTTPS) state — Windows SChannel only.
#ifdef _WIN32
    std::unique_ptr<TlsContext> tls;
    bool tlsHandshaking = false;
    std::vector<uint8_t> tlsWriteBuffer;
#endif

    std::shared_ptr<HttpRequestContext> selfGuard;

    HttpRequestContext(std::shared_ptr<HttpClient> c);
    ~HttpRequestContext();

    std::string Begin(const std::string& targetUrl);
    void FinishError(const std::string& kind, const std::string& detail);
    void FinishSuccess();
    void CompleteResponse();
    void TryParse();
    void OnEof();
    void CloseHandles();
    void CheckAllClosed();
    void OnAllClosed();
    void BuildRequestText();
    bool ParseHeaders();
    std::string HeaderValue(const std::string& name) const;

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnResolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res);
    static void OnConnected(uv_connect_t* req, int status);
    static void OnWritten(uv_write_t* req, int status);
    static void OnTimeout(uv_timer_t* timer);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
};

struct HttpClient : std::enable_shared_from_this<HttpClient>
{
    std::shared_ptr<HttpManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool closed = false;

    std::shared_ptr<Lode::Signal> responseSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value responseProxy;
    Lode::Value errorProxy;

    std::shared_ptr<HttpClient> selfGuard;
    std::vector<std::shared_ptr<HttpRequestContext>> activeRequests;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void FireResponse(const std::shared_ptr<HttpResponseData>& res);
    
    Lode::Value MethodRequest(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodRequestAsync(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Table BuildResponseTable(Lode::State& vm, const std::shared_ptr<HttpResponseData>& res);
    Lode::Value MethodClose(Lode::State& vm);

    void RequestClose();
    void RemoveRequest(const std::shared_ptr<HttpRequestContext>& req);
};

Lode::Value WrapClient(Lode::State& vm, const std::shared_ptr<HttpClient>& client, const Lode::Table& methods);
Lode::Table BuildClientMethods(Lode::State& vm, const std::shared_ptr<HttpManager>& mgr);

} // namespace lodehttp
