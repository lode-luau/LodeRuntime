// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace lodehttp
{

struct ParsedUrl
{
    bool valid = false;
    std::string error;
    std::string scheme;
    std::string host;
    int port = -1;
    std::string path;
    std::string authority;
};

ParsedUrl ParseUrl(const std::string& raw);
std::string ResolveRedirect(const std::string& baseUrl, const std::string& location);
std::string NormalizeMethod(std::string method);

struct HeaderPair
{
    std::string name;
    std::string value;
};

struct HttpRequestOptions
{
    std::string method = "GET";
    std::vector<HeaderPair> headers;
    std::string body;
    uint64_t timeoutMs = 0;
    bool followRedirects = true;
    int64_t maxRedirects = 10;
};

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

class HttpManager : public std::enable_shared_from_this<HttpManager>
{
public:
    explicit HttpManager(lua_State* mainL);
    ~HttpManager();

    HttpManager(const HttpManager&) = delete;
    HttpManager& operator=(const HttpManager&) = delete;

    void Start(const std::string& url, const HttpRequestOptions& opts,
               const std::shared_ptr<HttpResponseData>& result,
               std::function<void()> onDone);

    void AbortAll();
    void Shutdown();

private:
    struct Session;
    struct Request;

    void StartSession(const std::shared_ptr<Session>& session, const std::string& url);
    void NotifySession(const std::shared_ptr<Session>& session);
    void ScheduleNotify(const std::shared_ptr<Session>& session);
    void Remove(const std::shared_ptr<Request>& request);

    lua_State* mainL_ = nullptr;
    std::vector<std::shared_ptr<Request>> requests_;
};

} // namespace lodehttp
