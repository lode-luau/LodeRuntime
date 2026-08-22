// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "CryptoProvider.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>

namespace lodecrypto
{

void SecureClear(std::vector<uint8_t>& bytes)
{
    volatile uint8_t* ptr = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) ptr[i] = 0;
    bytes.clear();
    bytes.shrink_to_fit();
}

bool ConstantTimeEqual(Bytes left, Bytes right)
{
    const size_t width = (std::max)(left.size, right.size);
    uint8_t difference = static_cast<uint8_t>(left.size ^ right.size);
    for (size_t i = 0; i < width; ++i)
    {
        const uint8_t a = i < left.size ? left.data[i] : 0;
        const uint8_t b = i < right.size ? right.data[i] : 0;
        difference = static_cast<uint8_t>(difference | (a ^ b));
    }
    return left.size == right.size && difference == 0;
}

ProviderResult<std::vector<uint8_t>> HexEncode(Bytes data)
{
    if (data.size > kMaxOutputBytes / 2) return ProviderResult<std::vector<uint8_t>>::Failure("encoded output exceeds limit");
    static constexpr char digits[] = "0123456789abcdef";
    std::vector<uint8_t> result;
    result.reserve(data.size * 2);
    for (size_t i = 0; i < data.size; ++i)
    {
        result.push_back(static_cast<uint8_t>(digits[data.data[i] >> 4]));
        result.push_back(static_cast<uint8_t>(digits[data.data[i] & 0x0f]));
    }
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> HexDecode(Bytes input)
{
    const std::string_view text(reinterpret_cast<const char*>(input.data), input.size);
    if ((text.size() & 1u) != 0) return ProviderResult<std::vector<uint8_t>>::Failure("hex input must have even length");
    if (text.size() / 2 > kMaxOutputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("decoded output exceeds limit");
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> result(text.size() / 2);
    for (size_t i = 0; i < result.size(); ++i)
    {
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return ProviderResult<std::vector<uint8_t>>::Failure("hex input contains an invalid character");
        result[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> Base64Encode(Bytes data)
{
    if (data.size > (kMaxOutputBytes / 4) * 3) return ProviderResult<std::vector<uint8_t>>::Failure("encoded output exceeds limit");
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    result.reserve(((data.size + 2) / 3) * 4);
    for (size_t i = 0; i < data.size; i += 3)
    {
        const size_t remaining = data.size - i;
        const uint32_t value = (static_cast<uint32_t>(data.data[i]) << 16) |
            (remaining > 1 ? static_cast<uint32_t>(data.data[i + 1]) << 8 : 0) |
            (remaining > 2 ? data.data[i + 2] : 0);
        result.push_back(table[(value >> 18) & 63]);
        result.push_back(table[(value >> 12) & 63]);
        result.push_back(remaining > 1 ? table[(value >> 6) & 63] : '=');
        result.push_back(remaining > 2 ? table[value & 63] : '=');
    }
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> Base64Decode(Bytes input)
{
    const std::string_view text(reinterpret_cast<const char*>(input.data), input.size);
    if ((text.size() & 3u) != 0) return ProviderResult<std::vector<uint8_t>>::Failure("base64 input length must be a multiple of four");
    size_t padding = 0;
    if (!text.empty() && text.back() == '=') ++padding;
    if (text.size() > 1 && text[text.size() - 2] == '=') ++padding;
    if (padding > 2 || text.size() / 4 * 3 < padding) return ProviderResult<std::vector<uint8_t>>::Failure("base64 padding is invalid");
    if (text.size() / 4 * 3 - padding > kMaxOutputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("decoded output exceeds limit");
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> result;
    result.reserve(text.size() / 4 * 3 - padding);
    for (size_t i = 0; i < text.size(); i += 4)
    {
        const bool last = i + 4 == text.size();
        const int a = value(text[i]);
        const int b = value(text[i + 1]);
        const int c = text[i + 2] == '=' ? 0 : value(text[i + 2]);
        const int d = text[i + 3] == '=' ? 0 : value(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (!last && (text[i + 2] == '=' || text[i + 3] == '=')))
            return ProviderResult<std::vector<uint8_t>>::Failure("base64 input contains an invalid character");
        if (last && ((text[i + 2] == '=' && text[i + 3] != '=') ||
            (text[i + 2] == '=' && (b & 15) != 0) ||
            (text[i + 3] == '=' && text[i + 2] != '=' && (c & 3) != 0)))
            return ProviderResult<std::vector<uint8_t>>::Failure("base64 padding is invalid");
        const uint32_t packed = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
            (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
        result.push_back(static_cast<uint8_t>(packed >> 16));
        if (text[i + 2] != '=') result.push_back(static_cast<uint8_t>(packed >> 8));
        if (text[i + 3] != '=') result.push_back(static_cast<uint8_t>(packed));
    }
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> PemEncode(std::string_view label, Bytes der)
{
    if (label.empty() || label.size() > 64 || der.size > kMaxOutputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("invalid PEM parameters");
    for (char c : label) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-')) return ProviderResult<std::vector<uint8_t>>::Failure("invalid PEM label");
    auto encoded = Base64Encode(der); if (!encoded.ok) return ProviderResult<std::vector<uint8_t>>::Failure(encoded.error.message);
    std::string output = "-----BEGIN " + std::string(label) + "-----\n";
    for (size_t i = 0; i < encoded.value.size(); i += 64) { output.append(reinterpret_cast<const char*>(encoded.value.data() + i), (std::min)(size_t(64), encoded.value.size() - i)); output.push_back('\n'); }
    output += "-----END " + std::string(label) + "-----\n";
    if (output.size() > kMaxOutputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("encoded PEM exceeds limit");
    return ProviderResult<std::vector<uint8_t>>::Success(std::vector<uint8_t>(output.begin(), output.end()));
}

ProviderResult<std::vector<uint8_t>> PemDecode(Bytes input, std::string_view expectedLabel)
{
    const std::string_view pem(reinterpret_cast<const char*>(input.data), input.size);
    if (expectedLabel.empty() || expectedLabel.size() > 64 || pem.size() > kMaxInputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("invalid PEM parameters");
    const std::string begin = "-----BEGIN " + std::string(expectedLabel) + "-----";
    const std::string end = "-----END " + std::string(expectedLabel) + "-----";
    if (pem.substr(0, begin.size()) != begin) return ProviderResult<std::vector<uint8_t>>::Failure("PEM header mismatch");
    size_t bodyStart = begin.size(); if (bodyStart < pem.size() && pem[bodyStart] == '\r') ++bodyStart; if (bodyStart >= pem.size() || pem[bodyStart] != '\n') return ProviderResult<std::vector<uint8_t>>::Failure("invalid PEM header"); ++bodyStart;
    const size_t endPos = pem.find(end, bodyStart); if (endPos == std::string_view::npos) return ProviderResult<std::vector<uint8_t>>::Failure("PEM footer missing");
    std::string body; body.reserve(endPos - bodyStart);
    for (size_t i = bodyStart; i < endPos; ++i) if (pem[i] != '\r' && pem[i] != '\n' && pem[i] != ' ' && pem[i] != '\t') body.push_back(pem[i]);
    size_t footerEnd = endPos + end.size(); if (footerEnd < pem.size() && pem[footerEnd] == '\r') ++footerEnd; if (footerEnd >= pem.size() || pem[footerEnd] != '\n' || footerEnd + 1 != pem.size()) return ProviderResult<std::vector<uint8_t>>::Failure("invalid PEM trailing data");
    const Bytes bodyBytes{reinterpret_cast<const uint8_t*>(body.data()), body.size()};
    return Base64Decode(bodyBytes);
}

bool IsHashAlgorithm(std::string_view algorithm)
{
    return algorithm == "sha256" || algorithm == "sha384" || algorithm == "sha512";
}

size_t HashSize(std::string_view algorithm)
{
    if (algorithm == "sha256") return 32;
    if (algorithm == "sha384") return 48;
    if (algorithm == "sha512") return 64;
    return 0;
}

} // namespace lodecrypto
