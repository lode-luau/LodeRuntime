// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Luau/ParseResult.h"
#include "Luau/Linter.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace Lode
{

/**
 * @brief Represents the severity level of a log message.
 */
enum class LogLevel
{
    Info,
    Success,
    Warn,
    Error
};

/**
 * @brief Represents a secondary label or context pointing to a specific location in source code.
 */
struct DiagnosticLabel
{
    int line = 1;
    int column = 1;
    int length = 1;
    std::string message;
};

/**
 * @brief Represents a rich compiler or runtime diagnostic message (errors, warnings, lints).
 * 
 * Used by the Logger to render Rust-style beautiful CLI error reports.
 */
struct Diagnostic
{
    std::string filePath;
    int line = 1;
    int column = 1;
    int length = 1;
    std::string message;
    std::string code; // e.g. "E0001" or "SyntaxError"
    bool isWarning = false; // if true, diagnostic is non-blocking (warning); if false, it is a hard error
    std::vector<DiagnosticLabel> labels;
    std::vector<std::string> notes;
    std::vector<std::string> helps;
    std::vector<std::string> stackTrace;
};

/**
 * @brief Core logging and diagnostic reporting facility.
 */
class LODE_API Logger
{
public:
    /** @brief Initializes the logger (e.g. enabling virtual terminal processing for colors). */
    static void Initialize();

    /** @brief Logs an informational message. */
    static void Info(std::string_view message);
    /** @brief Logs a success message. */
    static void Success(std::string_view message);
    /** @brief Logs a warning message. */
    static void Warn(std::string_view message);
    /** @brief Logs an error message. */
    static void Error(std::string_view message);

    /** @brief Renders a rich Diagnostic to the console. */
    static void EmitDiagnostic(const Diagnostic& diag);
    /** @brief Renders a fatal C++ or Lua crash report. */
    static void EmitCrashReport(std::string_view title, std::string_view codeStr, void* address, const std::vector<std::string>& stackTrace, std::string_view details = "");

    // Utility helper to parse Luau error strings like "path/file.luau:8: message" or "path/file.luau:8:12: message"
    static Diagnostic ParseLuauError(std::string_view rawError, std::string_view defaultFilePath = "");

    // Constructs a precise Diagnostic from a Luau ParseError AST node
    static Diagnostic FromParseError(const Luau::ParseError& parseError, std::string_view filePath = "");

    // Constructs a precise Diagnostic from a Luau LintWarning node
    static Diagnostic FromLintWarning(const Luau::LintWarning& lintWarning, std::string_view filePath = "");

    // Constructs a precise Diagnostic from a Luau TypeError node
    static Diagnostic FromTypeError(const Luau::TypeError& typeError, std::string_view filePath = "", Luau::FileResolver* fileResolver = nullptr);

private:
    static bool colorEnabled_;
};

} // namespace Lode
