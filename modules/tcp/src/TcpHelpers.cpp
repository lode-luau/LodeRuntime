// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include <cstring>
#include "Tcp/TcpHelpers.hpp"
#include "uv.h"
#include <cmath>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

namespace lodetcp
{

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

int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen)
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

bool IsValidPort(double value, bool allowZero)
{
    double minPort = allowZero ? 0.0 : 1.0;
    return std::isfinite(value) && std::trunc(value) == value && value >= minPort && value <= 65535.0;
}

} // namespace lodetcp
