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

enum class LogLevel
{
    Info,
    Success,
    Warn,
    Error
};

struct DiagnosticLabel
{
    int line = 1;
    int column = 1;
    int length = 1;
    std::string message;
};

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

class LODE_API Logger
{
public:
    static void Initialize();

    static void Info(std::string_view message);
    static void Success(std::string_view message);
    static void Warn(std::string_view message);
    static void Error(std::string_view message);

    static void EmitDiagnostic(const Diagnostic& diag);
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
