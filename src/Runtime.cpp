// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Result.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "PathUtil.hpp"
#include "Platform/CrashHandler.hpp"

#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

using Lode::Detail::PathToUtf8;

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    // Initialize cross-platform CrashHandler and Logger ANSI support
    Lode::Platform::CrashHandler::Initialize();
    Lode::Logger::Initialize();

    if (argc < 2)
    {
        Lode::Logger::Info("LodeRuntime (lode_runtime) v1.0.0");
        Lode::Logger::Info("Usage: lode_runtime <file.luac | file.luau>");
        return 1;
    }

    fs::path filePath = fs::path(argv[1]);
    std::string filePathUtf8 = PathToUtf8(filePath);
    if (!fs::exists(filePath))
    {
        Lode::Diagnostic diag;
        diag.filePath = filePathUtf8;
        diag.message = "File not found: " + filePathUtf8;
        diag.code = "E0001";
        diag.helps.push_back("Check if the target path and filename are correct.");
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    if (!fs::is_regular_file(filePath))
    {
        Lode::Diagnostic diag;
        diag.filePath = filePathUtf8;
        diag.message = fs::is_directory(filePath)
            ? "Path is a directory, expected a .luau or .luac file: " + filePathUtf8
            : "Path is not a regular file: " + filePathUtf8;
        diag.code = "E0003";
        diag.helps.push_back("Pass a .luau or .luac file as the script argument.");
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        Lode::Diagnostic diag;
        diag.filePath = filePathUtf8;
        diag.message = "Failed to open file: " + filePathUtf8;
        diag.code = "E0002";
        diag.helps.push_back("Verify file permissions and accessibility.");
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string bytecode;
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".luau")
    {
        // Cached compile: on a cache hit the source is not type-checked again,
        // so warm runs start near-instantly. Diagnostics are only produced on a
        // cache miss (first run or when the file changed).
        std::vector<Lode::Diagnostic> diagnostics;
        bytecode = Lode::Compiler::CompileWithCache(content, filePathUtf8, nullptr, &diagnostics);

        bool hasErrors = false;
        for (const auto& diag : diagnostics)
        {
            Lode::Logger::EmitDiagnostic(diag);
            if (!diag.isWarning)
            {
                hasErrors = true;
            }
        }

        if (hasErrors || bytecode.empty())
        {
            return 1;
        }
    }
    else
    {
        bytecode = content;
    }

    auto stateResult = Lode::State::Create();
    if (stateResult.IsError())
    {
        Lode::Diagnostic diag;
        diag.message = "Error initializing runtime state: " + stateResult.GetError().ErrorMessage();
        diag.code = "VMInitError";
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    Lode::State vm = std::move(stateResult.GetValue());

    fs::path absPath = fs::absolute(filePath);
    std::string absPathUtf8 = PathToUtf8(absPath);
    fs::path parentPath = absPath.parent_path();
    if (!parentPath.empty())
    {
        vm.AddModulePath(PathToUtf8(parentPath));
    }

    std::string chunkName = "@" + absPathUtf8;
    auto execResult = vm.ExecuteBytecode(bytecode, chunkName);
    if (execResult.IsError())
    {
        Lode::Diagnostic diag = Lode::Logger::ParseLuauError(execResult.GetError().ErrorMessage(), absPathUtf8);
        diag.code = "RuntimeError";
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    // Process all pending libuv timers, I/O events, and coroutine resumes
    vm.GetEventLoop().Run(vm);

    // If the top-level script failed while yielding (e.g. error() inside a coroutine
    // resumed by the event loop), the error is surfaced here instead of being lost.
    std::string mainError = Lode::Task::GetMainThreadError(vm);
    if (!mainError.empty())
    {
        Lode::Diagnostic diag = Lode::Logger::ParseLuauError(mainError, absPathUtf8);
        diag.code = "RuntimeError";
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    return 0;
}
