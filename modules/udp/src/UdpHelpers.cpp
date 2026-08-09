// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Udp/UdpHelpers.hpp"
#include "uv.h"
#include <cmath>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

namespace lodeudp
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

bool IsValidPort(double value, bool allowZero)
{
    double minPort = allowZero ? 0.0 : 1.0;
    return std::isfinite(value) && std::trunc(value) == value && value >= minPort && value <= 65535.0;
}

} // namespace lodeudp
