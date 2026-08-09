// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TcpExport.hpp"
#include <string>

struct sockaddr;

namespace lodetcp
{

TCP_API std::string FormatIpAddress(const struct sockaddr* addr);
TCP_API std::string FormatSockAddr(const struct sockaddr* addr, int& portOut);
TCP_API bool IsValidPort(double value, bool allowZero = false);

} // namespace lodetcp
