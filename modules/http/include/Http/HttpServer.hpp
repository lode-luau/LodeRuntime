// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Http/HttpManager.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "uv.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace lodehttp
{

struct HttpServer;
struct HttpServerConnection;

struct HttpServerResponse : std::enable_shared_from_this<HttpServerResponse>
{
    std::weak_ptr<HttpServerConnection> weakConn;
    int statusCode = 200;
    std::string statusText = "OK";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string bodyBuffer;
    bool finished = false;

    void SetStatus(int code, const std::string& text = "");
    void SetHeader(const std::string& name, const std::string& value);
    void Write(const char* data, size_t len);
    void End(const char* data = nullptr, size_t len = 0);
};

struct HttpServerConnection : std::enable_shared_from_this<HttpServerConnection>
{
    HttpServer* server = nullptr;
    uv_tcp_t client{};
    bool clientInited = false;
    bool clientClosed = false;
    std::string rawBuffer;
    bool headersParsed = false;
    size_t headerEndPos = 0;
    size_t expectedContentLength = 0;
    bool hasContentLength = false;

    std::string method;
    std::string url;
    std::string path;
    std::string query;
    std::string httpVersion;
    std::vector<std::pair<std::string, std::string>> requestHeaders;
    std::string body;

    std::shared_ptr<HttpServerConnection> selfGuard;

    void StartReading();
    void StopReading();
    void OnData(const char* data, size_t len);
    bool TryParseRequest();
    void DispatchRequest();
    void SendRawResponse(const std::string& raw);
    void Close();

    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnClose(uv_handle_t* handle);
};

struct HttpServer : std::enable_shared_from_this<HttpServer>
{
    std::shared_ptr<HttpManager> mgr;
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

    std::shared_ptr<Lode::Signal> requestSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value requestProxy;
    Lode::Value errorProxy;

    Lode::Table responseMethods;

    std::vector<std::shared_ptr<HttpServerConnection>> connections;
    std::shared_ptr<HttpServer> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void UpdateAddresses();
    void BindFail(Lode::State& vm, const std::string& message);

    void AddConnection(const std::shared_ptr<HttpServerConnection>& conn);
    void RemoveConnection(const std::shared_ptr<HttpServerConnection>& conn);

    Lode::Value MethodListen(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodLocalAddress(Lode::State& vm);

    void RequestClose();
    void FinishClosed();

    static int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen);
    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnection(uv_stream_t* server, int status);
};

Lode::Value WrapServer(Lode::State& vm, const std::shared_ptr<HttpServer>& server, const Lode::Table& methods);
Lode::Table BuildServerMethods(Lode::State& vm, const std::shared_ptr<HttpManager>& mgr);
Lode::Table BuildResponseMethods(Lode::State& vm);
Lode::Value WrapServerResponse(Lode::State& vm, const std::shared_ptr<HttpServerResponse>& res, const Lode::Table& methods);

} // namespace lodehttp
