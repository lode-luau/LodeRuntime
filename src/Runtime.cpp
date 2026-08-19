// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Result.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "CiGenerator.hpp"
#include "PackageValidator.hpp"
#include "PackageLockfile.hpp"
#include "PathUtil.hpp"
#include "Platform/CrashHandler.hpp"

#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <array>

namespace fs = std::filesystem;

using Lode::Detail::PathToUtf8;

static fs::path FindStandardLibraryPath(const fs::path& executablePath)
{
    std::error_code ec;
    const fs::path executable = fs::weakly_canonical(fs::absolute(executablePath, ec), ec);
    if (ec)
        return {};

    const fs::path executableDirectory = executable.parent_path();
    const std::array<fs::path, 2> candidates = {
        executableDirectory.parent_path() / "stdlib",
        executableDirectory / "stdlib"
    };

    for (const fs::path& candidate : candidates)
    {
        if (fs::is_directory(candidate) && fs::is_regular_file(candidate / ".config.luau"))
            return fs::weakly_canonical(candidate, ec);
    }

    return {};
}

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    // Initialize cross-platform CrashHandler and Logger ANSI support
    Lode::Platform::CrashHandler::Initialize();
    Lode::Logger::Initialize();
    const fs::path standardLibraryPath = FindStandardLibraryPath(fs::path(argv[0]));

    if (argc < 2)
    {
        Lode::Logger::Info("Lode (lode) v1.0.0");
        Lode::Logger::Info("Usage: lode <file.luac | file.luau>");
        Lode::Logger::Info("       lode ci validate [--source|--artifact] [--locked] [package-root]");
        Lode::Logger::Info("       lode ci init [--force] [package-root]");
        Lode::Logger::Info("       lode ci update [package-root]");
        return 1;
    }

    const std::string firstArgument = PathToUtf8(fs::path(argv[1]));
    if (firstArgument == "ci")
    {
        if (argc < 3)
        {
            Lode::Logger::Error("Usage: lode ci validate [--source|--artifact] [--locked] [package-root]");
            Lode::Logger::Error("       lode ci init [--force] [package-root]");
            Lode::Logger::Error("       lode ci update [package-root]");
            return 1;
        }

        const std::string ciCommand = PathToUtf8(fs::path(argv[2]));
        if (ciCommand == "init")
        {
            bool force = false;
            fs::path packageRoot = fs::current_path();
            bool hasPackageRoot = false;
            for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
            {
                const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
                if (argument == "--force")
                {
                    force = true;
                }
                else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
                {
                    Lode::Logger::Error("Usage: lode ci init [--force] [package-root]");
                    return 1;
                }
                else
                {
                    packageRoot = fs::path(argv[argumentIndex]);
                    hasPackageRoot = true;
                }
            }

            Lode::Package::ValidationReport report = Lode::Package::GenerateWorkflow(
                packageRoot, force, standardLibraryPath);
            for (const std::string& warning : report.warnings)
                Lode::Logger::Warn(warning);
            for (const std::string& error : report.errors)
                Lode::Logger::Error(error);

            if (!report.IsValid())
                return 1;

            Lode::Logger::Success("Generated GitHub Actions workflow: " +
                PathToUtf8(fs::absolute(packageRoot) / ".github/workflows/lode.yml"));
            return 0;
        }

        if (ciCommand == "update")
        {
            fs::path packageRoot = fs::current_path();
            if (argc > 4)
            {
                Lode::Logger::Error("Usage: lode ci update [package-root]");
                return 1;
            }
            if (argc == 4)
                packageRoot = fs::path(argv[3]);

            Lode::Package::ValidationReport report = Lode::Package::UpdateWorkflow(
                packageRoot, standardLibraryPath);
            for (const std::string& warning : report.warnings)
                Lode::Logger::Warn(warning);
            for (const std::string& error : report.errors)
                Lode::Logger::Error(error);

            if (!report.IsValid())
                return 1;

            Lode::Logger::Success("Updated managed GitHub Actions workflow: " +
                PathToUtf8(fs::absolute(packageRoot) / ".github/workflows/lode.yml"));
            return 0;
        }

        if (ciCommand != "validate")
        {
            Lode::Logger::Error("Usage: lode ci validate [--source|--artifact] [--locked] [package-root]");
            Lode::Logger::Error("       lode ci init [--force] [package-root]");
            Lode::Logger::Error("       lode ci update [package-root]");
            return 1;
        }

        Lode::Package::ValidationMode mode = Lode::Package::ValidationMode::Artifact;
        bool locked = false;
        fs::path packageRoot = fs::current_path();
        bool hasPackageRoot = false;
        for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
            if (argument == "--source")
            {
                mode = Lode::Package::ValidationMode::Source;
            }
            else if (argument == "--artifact")
            {
                mode = Lode::Package::ValidationMode::Artifact;
            }
            else if (argument == "--locked")
            {
                locked = true;
            }
            else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
            {
                Lode::Logger::Error("Usage: lode ci validate [--source|--artifact] [--locked] [package-root]");
                return 1;
            }
            else
            {
                packageRoot = fs::path(argv[argumentIndex]);
                hasPackageRoot = true;
            }
        }

        Lode::Package::ValidationReport report = Lode::Package::Validate(
            packageRoot, mode, standardLibraryPath);
        for (const std::string& warning : report.warnings)
            Lode::Logger::Warn(warning);
        for (const std::string& error : report.errors)
            Lode::Logger::Error(error);

        if (!report.IsValid())
            return 1;

        if (locked)
        {
            const bool hasDependencies = report.dependencyGraph.packages.size() > 1 ||
                (!report.dependencyGraph.packages.empty() &&
                    !report.dependencyGraph.packages[report.dependencyGraph.root].dependencies.empty());
            const fs::path lockfilePath = fs::absolute(packageRoot) / "lode.lock";
            if (hasDependencies || fs::is_regular_file(lockfilePath))
            {
                Lode::Package::LockfileResult lockfile =
                    Lode::Package::ValidateLockfile(lockfilePath, report.dependencyGraph);
                for (const std::string& error : lockfile.errors)
                    Lode::Logger::Error(error);

                if (!lockfile.IsValid())
                    return 1;

                Lode::Logger::Success("Package lockfile validation passed: " +
                    PathToUtf8(lockfilePath));
            }
        }

        Lode::Logger::Success("Package validation passed: " + PathToUtf8(fs::absolute(packageRoot)));
        return 0;
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

    if (!standardLibraryPath.empty())
        vm.SetStandardLibraryPath(PathToUtf8(standardLibraryPath));

    std::vector<std::string> scriptArgs;
    for (int i = 2; i < argc; ++i)
    {
#if defined(_WIN32)
        scriptArgs.push_back(PathToUtf8(fs::path(argv[i])));
#else
        scriptArgs.push_back(std::string(argv[i]));
#endif
    }
    vm.SetCliArgs(scriptArgs);

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
