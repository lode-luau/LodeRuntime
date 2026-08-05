// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Result.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "Platform/CrashHandler.hpp"

#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
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

    std::string filePath = argv[1];
    if (!fs::exists(filePath))
    {
        Lode::Diagnostic diag;
        diag.filePath = filePath;
        diag.message = "File not found: " + filePath;
        diag.code = "E0001";
        diag.helps.push_back("Check if the target path and filename are correct.");
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        Lode::Diagnostic diag;
        diag.filePath = filePath;
        diag.message = "Failed to open file: " + filePath;
        diag.code = "E0002";
        diag.helps.push_back("Verify file permissions and accessibility.");
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string bytecode;
    if (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".luau")
    {
        // Cached compile: on a cache hit the source is not type-checked again,
        // so warm runs start near-instantly. Diagnostics are only produced on a
        // cache miss (first run or when the file changed).
        std::vector<Lode::Diagnostic> diagnostics;
        bytecode = Lode::Compiler::CompileWithCache(content, filePath, nullptr, &diagnostics);

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
    fs::path parentPath = absPath.parent_path();
    if (!parentPath.empty())
    {
        vm.AddModulePath(parentPath.string());
    }

    std::string chunkName = "@" + absPath.string();
    auto execResult = vm.ExecuteBytecode(bytecode, chunkName);
    if (execResult.IsError())
    {
        Lode::Diagnostic diag = Lode::Logger::ParseLuauError(execResult.GetError().ErrorMessage(), absPath.string());
        diag.code = "RuntimeError";
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    // Process all pending libuv timers, I/O events, and coroutine resumes
    Lode::EventLoop::Default().Run(vm);

    // If the top-level script failed while yielding (e.g. error() inside a coroutine
    // resumed by the event loop), the error is surfaced here instead of being lost.
    std::string mainError = Lode::Task::GetMainThreadError(vm);
    if (!mainError.empty())
    {
        Lode::Diagnostic diag = Lode::Logger::ParseLuauError(mainError, absPath.string());
        diag.code = "RuntimeError";
        Lode::Logger::EmitDiagnostic(diag);
        return 1;
    }

    return 0;
}
