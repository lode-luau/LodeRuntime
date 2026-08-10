// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Http/HttpTypes.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include <string>

namespace lodehttp
{

struct ParsedUrl
{
    bool valid = false;
    std::string error;
    std::string scheme;
    std::string host;
    int port = -1;
    std::string path;
    std::string authority;
};

ParsedUrl ParseUrl(const std::string& raw);
std::string ResolveRedirect(const std::string& baseUrl, const std::string& location);
std::string NormalizeMethod(std::string method);

// Extracts HttpRequestOptions from a Lode::Value (Luau table) containing
// FetchOptions / RequestOptions fields.
bool ParseFetchOptions(Lode::State& vm, const Lode::Value& val, HttpRequestOptions& opts);

} // namespace lodehttp
