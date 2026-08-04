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
#include <vector>

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
     * @brief Compiles source code with caching based on a SHA-256 of the content.
     * 
     * The cache key is derived from the SHA-256 of the source plus a fingerprint
     * of the effective compile options (which reflect --!native/@native/--!optimize/
     * --!debug directives parsed from the source), so editing a module or toggling
     * its directives invalidates the cache deterministically.
     * 
     * On a cache hit the compiled bytecode is returned immediately without any
     * type checking. On a miss the source is compiled (with diagnostics when
     * `outDiagnostics` is provided) and the bytecode is stored in the cache.
     * 
     * @param source The Luau source code.
     * @param filePath The file path (used only for the cache file name).
     * @param options Custom compilation options.
     * @param outDiagnostics Optional vector to populate with diagnostics when the
     *        cache misses (skips type checking entirely on cache hits).
     * @return The compiled bytecode string.
     */
    static std::string CompileWithCache(std::string_view source, std::string_view filePath, const lua_CompileOptions* options = nullptr, std::vector<Diagnostic>* outDiagnostics = nullptr);
};

} // namespace Lode
