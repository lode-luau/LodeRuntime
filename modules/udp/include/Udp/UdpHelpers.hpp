// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "UdpExport.hpp"
#include <string>

struct sockaddr;

namespace lodeudp
{

UDP_API std::string FormatIpAddress(const struct sockaddr* addr);
UDP_API std::string FormatSockAddr(const struct sockaddr* addr, int& portOut);
UDP_API bool IsValidPort(double value, bool allowZero = false);

} // namespace lodeudp
