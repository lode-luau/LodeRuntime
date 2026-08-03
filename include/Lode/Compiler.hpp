// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Logger.hpp"
#include "Luau/ParseResult.h"
#include "Luau/Linter.h"
#include "Luau/Error.h"
#include "luacode.h"
#include <string>
#include <string_view>

namespace Lode
{

class LODE_API Compiler
{
public:
    static lua_CompileOptions GetDefaultOptions();
    
    // Parses commentary directives in source code (--!native, --!optimize, --!debug, etc.)
    static lua_CompileOptions ParseOptionsFromSource(std::string_view source, std::string_view filePath = "");

    // Compiles source code to Luau bytecode string using specified or parsed options
    static std::string Compile(std::string_view source, const lua_CompileOptions* options = nullptr, std::string_view filePath = "");

    // Compiles source code and retrieves rich Diagnostics (ParseErrors, Lints, and TypeErrors) using Luau Frontend
    static std::string CompileWithResult(std::string_view source, std::vector<Diagnostic>& outDiagnostics, const lua_CompileOptions* options = nullptr, std::string_view filePath = "");

    // Returns OS temporary cache directory for Lode bytecode cache (%TEMP%/lode_cache on Windows, /tmp/lode_cache on Linux)
    static std::string GetCacheDirectory();

    // Compiles source code with caching based on file modification timestamp
    static std::string CompileWithCache(std::string_view source, std::string_view filePath, const lua_CompileOptions* options = nullptr);
};

} // namespace Lode
