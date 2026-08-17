// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Http/HttpHelpers.hpp"

#include <cctype>
#include <charconv>

namespace lodehttp
{

namespace
{

std::string ToLowerAscii(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool IsSchemeChar(char c)
{
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '+' || c == '-' || c == '.';
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

    if (scheme == "http" || scheme == "https")
    {
        if (port < 0)
            port = (scheme == "https") ? 443 : 80;
        out.port = port;
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

bool ParseFetchOptions(Lode::State& vm, const Lode::Value& val, HttpRequestOptions& opts)
{
    if (val.IsNil())
        return true;

    if (!val.IsTable())
    {
        vm.RaiseError("fetch options must be a table");
        return false;
    }

    Lode::Table t = val.AsTable();

    // 1. HTTP method (normalized to uppercase)
    if (t.Has("method"))
    {
        auto m = t.Get("method");
        if (m.IsOk() && !m.GetValue().IsNil())
        {
            if (!m.GetValue().IsString())
            {
                vm.RaiseError("fetch options 'method' must be a string");
                return false;
            }
            opts.method = NormalizeMethod(m.GetValue().AsString());
        }
    }

    // 2. Request body
    if (t.Has("body"))
    {
        auto b = t.Get("body");
        if (b.IsOk() && !b.GetValue().IsNil())
        {
            if (b.GetValue().IsString())
            {
                opts.body = b.GetValue().AsString();
            }
            else if (b.GetValue().IsBuffer())
            {
                size_t size = 0;
                void* data = b.GetValue().AsBuffer(&size);
                if (data || size == 0)
                    opts.body.assign(static_cast<const char*>(data), size);
                else
                {
                    vm.RaiseError("fetch options 'body' buffer is invalid");
                    return false;
                }
            }
            else
            {
                vm.RaiseError("fetch options 'body' must be a string or buffer");
                return false;
            }
        }
    }

    // 3. Timeout in milliseconds
    if (t.Has("timeout"))
    {
        auto tm = t.Get("timeout");
        if (tm.IsOk() && !tm.GetValue().IsNil())
        {
            if (!tm.GetValue().IsNumber())
            {
                vm.RaiseError("fetch options 'timeout' must be a number");
                return false;
            }
            opts.timeoutMs = static_cast<uint64_t>(tm.GetValue().AsNumber());
        }
    }

    // 4. Redirect limit
    if (t.Has("maxRedirects"))
    {
        auto mr = t.Get("maxRedirects");
        if (mr.IsOk() && !mr.GetValue().IsNil())
        {
            if (!mr.GetValue().IsNumber())
            {
                vm.RaiseError("fetch options 'maxRedirects' must be a number");
                return false;
            }
            opts.maxRedirects = static_cast<int64_t>(mr.GetValue().AsNumber());
        }
    }

    // 5. HTTP headers
    if (t.Has("headers"))
    {
        auto h = t.Get("headers");
        if (h.IsOk() && !h.GetValue().IsNil())
        {
            if (!h.GetValue().IsTable())
            {
                vm.RaiseError("fetch options 'headers' must be a table");
                return false;
            }
            Lode::Table hTable = h.GetValue().AsTable();
            for (const std::string& key : hTable.GetKeys())
            {
                auto hv = hTable.Get(key);
                if (hv.IsOk() && !hv.GetValue().IsNil())
                {
                    if (!hv.GetValue().IsString())
                    {
                        vm.RaiseError(std::string("fetch options header '") + key + "' must be a string");
                        return false;
                    }
                    opts.headers.push_back({ key, hv.GetValue().AsString() });
                }
            }
        }
    }

    // 6. Streaming chunked upload
    if (t.Has("chunked") || t.Has("chunkedUpload"))
    {
        auto c = t.Has("chunked") ? t.Get("chunked") : t.Get("chunkedUpload");
        if (c.IsOk() && c.GetValue().IsBoolean())
            opts.chunkedUpload = c.GetValue().AsBoolean();
    }

    // 7. Incremental response onData callback
    if (t.Has("onData") || t.Has("onChunk"))
    {
        auto cb = t.Has("onData") ? t.Get("onData") : t.Get("onChunk");
        if (cb.IsOk() && cb.GetValue().IsFunction())
            opts.onData = cb.GetValue();
    }

    return true;
}

} // namespace lodehttp
