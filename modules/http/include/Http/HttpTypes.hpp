// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Value.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace lodehttp
{

struct HeaderPair
{
    std::string name;
    std::string value;
};

struct HttpRequestOptions
{
    std::string method = "GET";
    std::vector<HeaderPair> headers;
    std::string body;
    uint64_t timeoutMs = 0;
    bool followRedirects = true;
    int64_t maxRedirects = 10;
    bool chunkedUpload = false;
    bool keepAlive = true;
    Lode::Value onData;
};

} // namespace lodehttp
