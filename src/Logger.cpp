// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Lode
{

bool Logger::colorEnabled_ = true;

// ANSI Color Escape Codes
namespace Color
{
    constexpr const char* Reset       = "\033[0m";
    constexpr const char* Bold        = "\033[1m";
    constexpr const char* RedBold     = "\033[1;31m";
    constexpr const char* GreenBold   = "\033[1;32m";
    constexpr const char* YellowBold  = "\033[1;33m";
    constexpr const char* BlueBold    = "\033[1;34m";
    constexpr const char* MagentaBold = "\033[1;35m";
    constexpr const char* CyanBold    = "\033[1;36m";
    constexpr const char* Gray        = "\033[90m";
}

void Logger::Initialize()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE)
    {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode))
        {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE)
    {
        DWORD dwMode = 0;
        if (GetConsoleMode(hErr, &dwMode))
        {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hErr, dwMode);
        }
    }
#endif
}

void Logger::Info(std::string_view message)
{
    std::cout << Color::CyanBold << "info" << Color::Reset << ": " << message << "\n";
}

void Logger::Success(std::string_view message)
{
    std::cout << Color::GreenBold << "success" << Color::Reset << ": " << message << "\n";
}

void Logger::Warn(std::string_view message)
{
    std::cerr << Color::YellowBold << "warning" << Color::Reset << ": " << message << "\n";
}

void Logger::Error(std::string_view message)
{
    std::cerr << Color::RedBold << "error" << Color::Reset << ": " << message << "\n";
}

static std::vector<std::string> ReadSourceLines(const std::string& filePath)
{
    std::vector<std::string> lines;
    if (filePath.empty() || !fs::exists(filePath)) return lines;

    std::ifstream file(filePath);
    if (!file.is_open()) return lines;

    std::string line;
    while (std::getline(file, line))
    {
        lines.push_back(line);
    }
    return lines;
}

void Logger::EmitDiagnostic(const Diagnostic& diag)
{
    Initialize();

    bool isWarn = diag.isWarning
                || (diag.code.rfind("Warning", 0) != std::string::npos)
                || (diag.code.rfind("Lint", 0) != std::string::npos);

    if (isWarn)
    {
        std::cerr << Color::YellowBold << "warning";
    }
    else
    {
        std::cerr << Color::RedBold << "error";
    }

    if (!diag.code.empty())
    {
        std::cerr << "[" << diag.code << "]";
    }
    std::cerr << Color::Reset << Color::Bold << ": " << diag.message << Color::Reset << "\n";

    if (!diag.filePath.empty())
    {
        std::cerr << Color::CyanBold << "  --> " << Color::Reset << diag.filePath;
        if (diag.line > 0)
        {
            std::cerr << ":" << diag.line;
            if (diag.column > 0)
            {
                std::cerr << ":" << diag.column;
            }
        }
        std::cerr << "\n";
    }

    std::vector<std::string> sourceLines;
    if (!diag.filePath.empty())
    {
        sourceLines = ReadSourceLines(diag.filePath);
    }

    if (!sourceLines.empty() && diag.line > 0 && diag.line <= static_cast<int>(sourceLines.size()))
    {
        int lineIdx = diag.line - 1;
        std::string lineStr = std::to_string(diag.line);
        size_t padWidth = lineStr.length() > 2 ? lineStr.length() : 2;
        std::string padding(padWidth, ' ');

        std::cerr << Color::CyanBold << padding << " |\n" << Color::Reset;

        // Print the original source line
        std::cerr << Color::CyanBold << diag.line;
        for (size_t i = lineStr.length(); i < padWidth; ++i) std::cerr << " ";
        std::cerr << " | " << Color::Reset << sourceLines[lineIdx] << "\n";

        // Print the underline marker ^^^^^
        std::cerr << Color::CyanBold << padding << " | " << Color::Reset;

        int col = diag.column > 0 ? diag.column : 1;
        int len = diag.length > 0 ? diag.length : 1;

        for (int i = 1; i < col; ++i)
        {
            if (i <= static_cast<int>(sourceLines[lineIdx].length()) && sourceLines[lineIdx][i - 1] == '\t')
                std::cerr << "\t";
            else
                std::cerr << " ";
        }

        // Caret color: red for errors, yellow for warnings
        std::cerr << (isWarn ? Color::YellowBold : Color::RedBold);
        for (int i = 0; i < len; ++i) std::cerr << "^";
        std::cerr << Color::Reset;

        if (!diag.labels.empty())
        {
            std::cerr << Color::RedBold << " " << diag.labels[0].message << Color::Reset;
        }
        std::cerr << "\n";
        std::cerr << Color::CyanBold << padding << " |\n" << Color::Reset;
    }

    // Stack Trace
    if (!diag.stackTrace.empty())
    {
        std::cerr << Color::CyanBold << "   --- Stack Trace ---\n" << Color::Reset;
        for (size_t i = 0; i < diag.stackTrace.size(); ++i)
        {
            std::cerr << Color::Gray << "   [" << i << "] " << Color::Reset << diag.stackTrace[i] << "\n";
        }
        std::cerr << Color::CyanBold << "   -------------------\n" << Color::Reset;
    }

    // Notes & Helps
    for (const auto& note : diag.notes)
    {
        std::cerr << Color::CyanBold << "   = " << Color::GreenBold << "note" << Color::Reset << ": " << note << "\n";
    }
    for (const auto& help : diag.helps)
    {
        std::cerr << Color::CyanBold << "   = " << Color::GreenBold << "help" << Color::Reset << ": " << help << "\n";
    }
    std::cerr << "\n";
}

void Logger::EmitCrashReport(std::string_view title, std::string_view codeStr, void* address, const std::vector<std::string>& stackTrace, std::string_view details)
{
    Initialize();

    std::cerr << "\n" << Color::RedBold << "=======================================================\n";
    std::cerr << "         [LODERUNTIME CRASH DETECTED]                  \n";
    std::cerr << "=======================================================\n" << Color::Reset;
    
    std::cerr << Color::Bold << "Crash Reason     : " << Color::RedBold << title << Color::Reset << "\n";
    std::cerr << Color::Bold << "Exception Code   : " << Color::YellowBold << codeStr << Color::Reset << "\n";
    std::cerr << Color::Bold << "Faulting Address : " << Color::CyanBold << "0x" << std::hex << reinterpret_cast<uintptr_t>(address) << std::dec << Color::Reset << "\n";

    if (!details.empty())
    {
        std::cerr << Color::Bold << "Details          : " << Color::Reset << details << "\n";
    }

    std::cerr << "\n" << Color::CyanBold << "--- Stack Trace ---" << Color::Reset << "\n";
    if (stackTrace.empty())
    {
        std::cerr << Color::Gray << "  (No symbol backtrace available)" << Color::Reset << "\n";
    }
    else
    {
        for (size_t i = 0; i < stackTrace.size(); ++i)
        {
            std::cerr << Color::Gray << "  [" << i << "] " << Color::Reset << stackTrace[i] << "\n";
        }
    }

    std::cerr << Color::RedBold << "=======================================================\n\n" << Color::Reset;
}

// Parses Luau's "<path>:<line>:<column>: <message>" runtime error format into a
// structured Diagnostic. When the string carries no location, the supplied
// defaultFilePath is kept and the whole string becomes the message.
Diagnostic Logger::ParseLuauError(std::string_view rawError, std::string_view defaultFilePath)
{
    Diagnostic diag;
    diag.filePath = std::string(defaultFilePath);
    std::string errStr(rawError);

    // Strip common Lode/Luau error prefixes
    const std::vector<std::string> prefixes = {
        "Bytecode load failed: ",
        "Execution failed: ",
        "C++ callback exception: ",
        "Coroutine execution error: "
    };

    bool stripped;
    do
    {
        stripped = false;
        for (const auto& prefix : prefixes)
        {
            size_t p = errStr.find(prefix);
            if (p != std::string::npos)
            {
                errStr = errStr.substr(p + prefix.length());
                stripped = true;
                break; // break the inner loop to restart checking from the new beginning
            }
        }
    } while (stripped);

    // If the error starts with ':' (e.g. ":1: Expected..."), prepend the file path
    if (errStr.rfind(":", 0) == 0 && !diag.filePath.empty())
    {
        errStr = diag.filePath + errStr;
    }

    size_t firstNewline = errStr.find('\n');
    std::string firstLine = (firstNewline != std::string::npos) ? errStr.substr(0, firstNewline) : errStr;

    size_t lineColon = std::string::npos;
    for (size_t i = 0; i < firstLine.length(); ++i)
    {
        if (firstLine[i] == ':' && i + 1 < firstLine.length() && std::isdigit(firstLine[i + 1]))
        {
            lineColon = i;
            break;
        }
    }

    bool parsedFromFirstLine = false;
    if (lineColon != std::string::npos)
    {
        size_t nextColon = firstLine.find(':', lineColon + 1);
        if (nextColon != std::string::npos)
        {
            diag.filePath = firstLine.substr(0, lineColon);
            std::string lineStr = firstLine.substr(lineColon + 1, nextColon - lineColon - 1);
            try
            {
                diag.line = std::stoi(lineStr);
                size_t msgColon = firstLine.find(':', nextColon + 1);
                if (msgColon != std::string::npos && std::isdigit(firstLine[nextColon + 1]))
                {
                    std::string colStr = firstLine.substr(nextColon + 1, msgColon - nextColon - 1);
                    diag.column = std::stoi(colStr);
                    // The rest of the whole error string (not just first line)
                    diag.message = errStr.substr(msgColon + 1);
                    // Trim leading spaces from message
                    size_t firstNonSpace = diag.message.find_first_not_of(" \t");
                    if (firstNonSpace != std::string::npos)
                        diag.message = diag.message.substr(firstNonSpace);
                }
                else
                {
                    diag.message = errStr.substr(nextColon + 1);
                    size_t firstNonSpace = diag.message.find_first_not_of(" \t");
                    if (firstNonSpace != std::string::npos)
                        diag.message = diag.message.substr(firstNonSpace);
                }
                parsedFromFirstLine = true;
            }
            catch (...)
            {
            }
        }
    }

    if (!parsedFromFirstLine)
    {
        diag.message = errStr;
        // Search subsequent lines for a traceback
        if (firstNewline != std::string::npos)
        {
            std::string secondLine = errStr.substr(firstNewline + 1);
            size_t secondNewline = secondLine.find('\n');
            if (secondNewline != std::string::npos)
            {
                secondLine = secondLine.substr(0, secondNewline);
            }
            
            size_t tbColon = std::string::npos;
            for (size_t i = 0; i < secondLine.length(); ++i)
            {
                if (secondLine[i] == ':' && i + 1 < secondLine.length() && std::isdigit(secondLine[i + 1]))
                {
                    tbColon = i;
                    break;
                }
            }
            
            if (tbColon != std::string::npos)
            {
                diag.filePath = secondLine.substr(0, tbColon);
                // line might be followed by space (e.g. `:20 function`)
                size_t spaceAfterLine = secondLine.find(' ', tbColon + 1);
                std::string lineStr = secondLine.substr(tbColon + 1, spaceAfterLine - tbColon - 1);
                try
                {
                    diag.line = std::stoi(lineStr);
                }
                catch (...)
                {
                }
            }
        }
    }

    // Strip leading whitespace from the message
    size_t startMsg = diag.message.find_first_not_of(" \t\r\n");
    if (startMsg != std::string::npos)
    {
        diag.message = diag.message.substr(startMsg);
    }

    return diag;
}

// Maps a Luau source location to 1-based line/column and a caret length.
static void ApplyLocation(Diagnostic& diag, const Luau::Location& loc)
{
    diag.line = loc.begin.line + 1;
    diag.column = loc.begin.column + 1;

    int beginCol = loc.begin.column;
    int endCol = loc.end.column;
    diag.length = (endCol > beginCol) ? (endCol - beginCol) : 1;
}

Diagnostic Logger::FromParseError(const Luau::ParseError& parseError, std::string_view filePath)
{
    Diagnostic diag;
    diag.filePath = std::string(filePath);
    ApplyLocation(diag, parseError.getLocation());
    diag.message = parseError.what();
    diag.code = "SyntaxError";

    return diag;
}

Diagnostic Logger::FromLintWarning(const Luau::LintWarning& lintWarning, std::string_view filePath)
{
    Diagnostic diag;
    diag.filePath = std::string(filePath);
    ApplyLocation(diag, lintWarning.location);
    diag.message = lintWarning.text;
    diag.code = std::string("Lint_") + Luau::LintWarning::getName(lintWarning.code);

    return diag;
}

Diagnostic Logger::FromTypeError(const Luau::TypeError& typeError, std::string_view filePath, Luau::FileResolver* fileResolver)
{
    Diagnostic diag;
    if (!typeError.moduleName.empty())
    {
        diag.filePath = typeError.moduleName;
    }
    else
    {
        diag.filePath = std::string(filePath);
    }

    Luau::Location loc = typeError.location;
    if (const Luau::TypeMismatch* tm = Luau::get_if<Luau::TypeMismatch>(&typeError.data))
    {
        if (tm->error)
        {
            loc = tm->error->location;
        }
    }

    ApplyLocation(diag, loc);

    Luau::TypeErrorToStringOptions options;
    options.fileResolver = fileResolver;
    diag.message = Luau::toString(typeError, options);
    diag.code = "TypeError";

    return diag;
}

} // namespace Lode
