// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ws
{

// Standard base64 with '=' padding (RFC 4648). Dependency-free; used to encode
// the Sec-WebSocket-Key and the Sec-WebSocket-Accept digest.
inline std::string Base64Encode(const uint8_t* data, size_t len)
{
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len)
    {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16)
                   | (static_cast<uint32_t>(data[i + 1]) << 8)
                   | static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (rem == 2)
    {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16)
                   | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

inline std::string Base64Encode(const std::string& input)
{
    return Base64Encode(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

} // namespace ws
