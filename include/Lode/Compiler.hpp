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

/**
 * @brief Interfaces with Luau's bytecode compiler and frontend.
 * 
 * Provides static methods for converting Luau source code into bytecode,
 * caching compilation results, and extracting rich diagnostics.
 */
class LODE_API Compiler
{
public:
    /** @brief Retrieves the default compilation options used by Lode. */
    static lua_CompileOptions GetDefaultOptions();
    
    /** 
     * @brief Parses commentary directives in source code (e.g. --!native, --!optimize).
     * @param source The Luau source code.
     * @param filePath Optional file path for context.
     * @return The parsed compile options.
     */
    static lua_CompileOptions ParseOptionsFromSource(std::string_view source, std::string_view filePath = "");

    /** 
     * @brief Compiles source code to a Luau bytecode string.
     * @param source The Luau source code.
     * @param options Custom compilation options (if null, options are parsed from source).
     * @param filePath Optional file path for context.
     * @return The compiled bytecode string.
     */
    static std::string Compile(std::string_view source, const lua_CompileOptions* options = nullptr, std::string_view filePath = "");

    /** 
     * @brief Compiles source code and retrieves rich Diagnostics (ParseErrors, Lints, and TypeErrors).
     * @param source The Luau source code.
     * @param outDiagnostics A vector to populate with compiler diagnostics.
     * @param options Custom compilation options.
     * @param filePath Optional file path for context.
     * @return The compiled bytecode string.
     */
    static std::string CompileWithResult(std::string_view source, std::vector<Diagnostic>& outDiagnostics, const lua_CompileOptions* options = nullptr, std::string_view filePath = "");

    /** 
     * @brief Returns the OS temporary cache directory for Lode bytecode cache.
     * @return The path to the cache directory.
     */
    static std::string GetCacheDirectory();

    /** 
     * @brief Compiles source code with caching based on the file's modification timestamp.
     * @param source The Luau source code.
     * @param filePath The file path (required for cache hashing and timestamps).
     * @param options Custom compilation options.
     * @return The compiled bytecode string.
     */
    static std::string CompileWithCache(std::string_view source, std::string_view filePath, const lua_CompileOptions* options = nullptr);
};

} // namespace Lode
