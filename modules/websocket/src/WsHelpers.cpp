// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "WebSocket/WsHelpers.hpp"
#include "sha1.hpp"
#include "base64.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace lodews
{

std::string FormatIpAddress(const struct sockaddr* addr)
{
    char ip[INET6_ADDRSTRLEN] = {0};
    if (addr->sa_family == AF_INET)
        uv_ip4_name(reinterpret_cast<const struct sockaddr_in*>(addr), ip, sizeof(ip));
    else if (addr->sa_family == AF_INET6)
        uv_ip6_name(reinterpret_cast<const struct sockaddr_in6*>(addr), ip, sizeof(ip));
    return std::string(ip);
}

std::string FormatSockAddr(const struct sockaddr* addr, int& portOut)
{
    if (addr->sa_family == AF_INET)
        portOut = ntohs(reinterpret_cast<const struct sockaddr_in*>(addr)->sin_port);
    else if (addr->sa_family == AF_INET6)
        portOut = ntohs(reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_port);
    else
        portOut = 0;
    return FormatIpAddress(addr);
}

bool IsValidPort(double value, bool allowZero)
{
    if (std::isnan(value) || !std::isfinite(value))
        return false;
    if (std::floor(value) != value)
        return false;
    if (allowZero && value == 0)
        return true;
    return value >= 1 && value <= 65535;
}

bool IsValidCloseCode(double value)
{
    if (std::isnan(value) || !std::isfinite(value))
        return false;
    if (std::floor(value) != value)
        return false;
    if (value == 1000)
        return true;
    return value >= 3000 && value <= 4999;
}

void WsRng::Seed(uint64_t seed)
{
    state = static_cast<uint32_t>(seed ^ (seed >> 32));
    if (state == 0)
        state = 0x811c9dc5;
}

uint32_t WsRng::Next()
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void WsRng::Fill(uint8_t* dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = static_cast<uint8_t>(Next() & 0xFF);
}

ParsedWsUrl ParseWebSocketUrl(const std::string& raw)
{
    ParsedWsUrl out;
    std::string s = raw;
    if (s.compare(0, 6, "wss://") == 0)
    {
        out.valid = false;
        out.error = "tls";
        return out;
    }
    std::string prefix = "ws://";
    if (s.compare(0, prefix.size(), prefix) != 0)
    {
        out.valid = false;
        out.error = "invalid-url";
        return out;
    }
    s = s.substr(prefix.size());
    if (s.empty())
    {
        out.valid = false;
        out.error = "invalid-url";
        return out;
    }
    size_t slash = s.find('/');
    std::string hostPort = s.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : s.substr(slash);
    std::string host;
    int port = 80;
    if (hostPort.empty())
    {
        out.valid = false;
        out.error = "invalid-url";
        return out;
    }
    if (hostPort[0] == '[')
    {
        size_t rbracket = hostPort.find(']');
        if (rbracket == std::string::npos)
        {
            out.valid = false;
            out.error = "invalid-url";
            return out;
        }
        host = hostPort.substr(0, rbracket + 1);
        if (rbracket + 1 < hostPort.size())
        {
            if (hostPort[rbracket + 1] != ':')
            {
                out.valid = false;
                out.error = "invalid-url";
                return out;
            }
            std::string pstr = hostPort.substr(rbracket + 2);
            try { port = std::stoi(pstr); }
            catch (...) { out.valid = false; out.error = "invalid-url"; return out; }
            if (!IsValidPort(port, false)) { out.valid = false; out.error = "invalid-url"; return out; }
        }
    }
    else
    {
        size_t colon = hostPort.find(':');
        if (colon != std::string::npos)
        {
            host = hostPort.substr(0, colon);
            std::string pstr = hostPort.substr(colon + 1);
            try { port = std::stoi(pstr); }
            catch (...) { out.valid = false; out.error = "invalid-url"; return out; }
            if (!IsValidPort(port, false)) { out.valid = false; out.error = "invalid-url"; return out; }
        }
        else
        {
            host = hostPort;
        }
    }
    out.valid = true;
    out.host = host;
    out.port = port;
    out.path = path;
    out.connectHost = host;
    if (!host.empty() && host[0] == '[' && host.back() == ']')
        out.connectHost = host.substr(1, host.size() - 2);
    return out;
}

const std::string* ParsedHeaders::Find(const std::string& name) const
{
    for (const auto& kv : fields)
    {
        if (EqualsIgnoreCase(kv.first, name))
            return &kv.second;
    }
    return nullptr;
}

bool ParsedHeaders::EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool ParseHeaderBlock(const std::string& block, ParsedHeaders& out)
{
    size_t lineStart = 0;
    size_t pos = block.find("\r\n");
    if (pos == std::string::npos)
        return false;
    out.statusLine = block.substr(0, pos);
    lineStart = pos + 2;

    while (lineStart < block.size())
    {
        size_t nl = block.find("\r\n", lineStart);
        if (nl == std::string::npos)
            return false;
        std::string line = block.substr(lineStart, nl - lineStart);
        if (line.empty())
            break;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return false;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        size_t first = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t");
        if (first != std::string::npos && last != std::string::npos)
            value = value.substr(first, last - first + 1);
        else
            value.clear();
        out.fields.emplace_back(name, value);
        lineStart = nl + 2;
    }
    return true;
}

bool HeaderListContains(const std::string* value, const std::string& token)
{
    if (!value)
        return false;
    size_t start = 0;
    while (start <= value->size())
    {
        size_t comma = value->find(',', start);
        std::string part = comma == std::string::npos ? value->substr(start) : value->substr(start, comma - start);
        size_t first = part.find_first_not_of(" \t");
        size_t last = part.find_last_not_of(" \t");
        if (first == std::string::npos)
        {
            if (comma == std::string::npos)
                break;
            start = comma + 1;
            continue;
        }
        std::string trimmed = part.substr(first, last - first + 1);
        if (ParsedHeaders::EqualsIgnoreCase(trimmed, token))
            return true;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

std::vector<char> EncodeFrame(uint8_t opcode, const std::vector<char>& payload, bool fin, bool masked, uint32_t maskKey)
{
    std::vector<char> out;
    size_t len = payload.size();
    out.reserve(len + 14);

    out.push_back(static_cast<char>((fin ? 0x80 : 0) | (opcode & 0x0F)));

    char b1 = static_cast<char>(masked ? 0x80 : 0);
    if (len < 126)
    {
        out.push_back(static_cast<char>(b1 | static_cast<char>(len)));
    }
    else if (len <= 0xFFFF)
    {
        out.push_back(static_cast<char>(b1 | 126));
        out.push_back(static_cast<char>((len >> 8) & 0xFF));
        out.push_back(static_cast<char>(len & 0xFF));
    }
    else
    {
        out.push_back(static_cast<char>(b1 | 127));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
    }

    if (masked)
    {
        out.push_back(static_cast<char>((maskKey >> 0) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 8) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 16) & 0xFF));
        out.push_back(static_cast<char>((maskKey >> 24) & 0xFF));
        for (size_t i = 0; i < len; ++i)
        {
            uint8_t keyByte = static_cast<uint8_t>((maskKey >> (8 * (i % 4))) & 0xFF);
            out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ keyByte));
        }
    }
    else
    {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

std::vector<char> MakeClosePayload(uint16_t code, const std::string& reason)
{
    std::vector<char> payload;
    payload.reserve(reason.size() + 2);
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    for (size_t i = 0; i < reason.size() && payload.size() < 125; ++i)
        payload.push_back(reason[i]);
    return payload;
}

std::string ComputeAcceptKey(const std::string& clientKey)
{
    auto digest = ws::Sha1(clientKey + kWebSocketGuid);
    return ws::Base64Encode(digest.data(), digest.size());
}

} // namespace lodews
