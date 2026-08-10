// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "WebSocketExport.hpp"
#include <string>
#include <vector>
#include <cstdint>

#include "uv.h"

namespace lodews
{

constexpr uint8_t kOpcodeContinuation = 0x0;
constexpr uint8_t kOpcodeText = 0x1;
constexpr uint8_t kOpcodeBinary = 0x2;
constexpr uint8_t kOpcodeClose = 0x8;
constexpr uint8_t kOpcodePing = 0x9;
constexpr uint8_t kOpcodePong = 0xA;

constexpr uint16_t kCloseNormal = 1000;
constexpr uint16_t kCloseProtocolError = 1002;
constexpr uint16_t kCloseAbnormal = 1006;

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum class WsState
{
    Connecting,
    Open,
    Closing,
    Closed,
};

enum class TimerMode
{
    None,
    Connect,
    Close,
};

std::string FormatIpAddress(const struct sockaddr* addr);
std::string FormatSockAddr(const struct sockaddr* addr, int& portOut);
bool IsValidPort(double value, bool allowZero = false);
bool IsValidCloseCode(double value);

struct WsRng
{
    uint32_t state = 0;
    void Seed(uint64_t seed);
    uint32_t Next();
    void Fill(uint8_t* dst, size_t n);
};

struct ParsedWsUrl
{
    bool valid = false;
    std::string error;
    std::string host;
    std::string connectHost;
    int port = 0;
    std::string path;
};

ParsedWsUrl ParseWebSocketUrl(const std::string& raw);

struct ParsedHeaders
{
    std::string statusLine;
    std::vector<std::pair<std::string, std::string>> fields;
    const std::string* Find(const std::string& name) const;
    static bool EqualsIgnoreCase(const std::string& a, const std::string& b);
};

bool ParseHeaderBlock(const std::string& block, ParsedHeaders& out);
bool HeaderListContains(const std::string* value, const std::string& token);

std::vector<char> EncodeFrame(uint8_t opcode, const std::vector<char>& payload, bool fin, bool masked, uint32_t maskKey);
std::vector<char> MakeClosePayload(uint16_t code, const std::string& reason);
std::string ComputeAcceptKey(const std::string& clientKey);

} // namespace lodews
