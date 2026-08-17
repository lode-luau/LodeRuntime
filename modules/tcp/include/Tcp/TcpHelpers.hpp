// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include <string>

struct sockaddr;
struct sockaddr_storage;

namespace lodetcp
{

TCP_API std::string FormatIpAddress(const struct sockaddr* addr);
TCP_API std::string FormatSockAddr(const struct sockaddr* addr, int& portOut);
TCP_API int MakeSockAddr(const std::string& host, int port, struct sockaddr_storage& out, int& outLen);
TCP_API bool IsValidPort(double value, bool allowZero = false);

} // namespace lodetcp
