// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ws
{

// RFC 3174 SHA-1. Header-only, dependency-free. Used to derive the
// Sec-WebSocket-Accept value: base64(SHA1(clientKey + GUID)).
inline std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len)
{
    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    size_t paddedLen = ((len + 8) / 64 + 1) * 64;
    std::vector<uint8_t> padded(paddedLen, 0);
    if (len > 0)
        std::copy(data, data + len, padded.begin());
    padded[len] = 0x80;

    uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i)
        padded[paddedLen - 1 - i] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFF);

    auto rol = [](uint32_t value, int bits) -> uint32_t {
        return (value << bits) | (value >> (32 - bits));
    };

    uint32_t w[80];
    for (size_t block = 0; block < paddedLen; block += 64)
    {
        for (int i = 0; i < 16; ++i)
        {
            size_t off = block + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(padded[off]) << 24)
                 | (static_cast<uint32_t>(padded[off + 1]) << 16)
                 | (static_cast<uint32_t>(padded[off + 2]) << 8)
                 | static_cast<uint32_t>(padded[off + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f;
            uint32_t k;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> out{};
    uint32_t h[] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
    {
        out[static_cast<size_t>(i) * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return out;
}

inline std::array<uint8_t, 20> Sha1(const std::string& input)
{
    return Sha1(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

} // namespace ws
