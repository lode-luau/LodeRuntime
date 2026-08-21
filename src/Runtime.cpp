// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Result.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "CiGenerator.hpp"
#include "GitResolver.hpp"
#include "PackageValidator.hpp"
#include "PackageLockfile.hpp"
#include "PackageInstaller.hpp"
#include "PackagePacker.hpp"
#include "ProjectInitializer.hpp"
#include "PathUtil.hpp"
#include "Platform/CrashHandler.hpp"

#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace fs = std::filesystem;

using Lode::Detail::PathToUtf8;

static fs::path FindStandardLibraryPath(const fs::path& executablePath)
{
    std::error_code ec;
    const fs::path executable = fs::weakly_canonical(fs::absolute(executablePath, ec), ec);
    if (ec)
        return {};

    const fs::path executableDirectory = executable.parent_path();
    const std::array<fs::path, 3> candidates = {
        executableDirectory.parent_path() / "stdlib",
        executableDirectory.parent_path().parent_path() / "stdlib",
        executableDirectory / "stdlib"
    };

    for (const fs::path& candidate : candidates)
    {
        if (fs::is_directory(candidate) && fs::is_regular_file(candidate / ".config.luau"))
            return fs::weakly_canonical(candidate, ec);
    }

    return {};
}

struct AddDependencyResult
{
    std::string alias;
    std::string repository;
    std::string requirement;
    std::vector<std::string> errors;

    bool IsValid() const
    {
        return errors.empty() && !alias.empty() && !repository.empty();
    }
};

void AddError(AddDependencyResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool IsSafePackageAlias(const std::string& alias)
{
    if (alias.empty() || alias == "." || alias == "..")
        return false;
    return std::all_of(alias.begin(), alias.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' ||
            character == '_' || character == '.';
    });
}

bool LooksLikeGitHubSlug(const std::string& value)
{
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        value.rfind(".", 0) == 0 || value.find_first_of("\\:") != std::string::npos)
        return false;

    const size_t separator = value.find('/');
    return separator != std::string::npos && separator > 0 &&
        value.find('/', separator + 1) == std::string::npos;
}

std::string RepositoryAlias(const std::string& repository)
{
    std::string value = repository;
    while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
        value.pop_back();
    if (value.size() > 4 && value.substr(value.size() - 4) == ".git")
        value.resize(value.size() - 4);

    const size_t separator = value.find_last_of("/\\:");
    const std::string alias = separator == std::string::npos
        ? value : value.substr(separator + 1);
    return alias;
}

AddDependencyResult ParseAddDependencySpec(const std::string& specification)
{
    AddDependencyResult result;
    if (specification.empty())
    {
        AddError(result, "Package specification cannot be empty.");
        return result;
    }

    std::string repository = specification;
    std::string requirement;
    const size_t at = specification.rfind('@');
    const size_t separator = specification.find_last_of("/\\:");
    if (at != std::string::npos && (separator == std::string::npos || at > separator))
    {
        repository = specification.substr(0, at);
        requirement = specification.substr(at + 1);
    }

    if (repository.rfind("github:", 0) == 0)
    {
        const std::string slug = repository.substr(7);
        if (!LooksLikeGitHubSlug(slug))
        {
            AddError(result, "GitHub package specification must use owner/repository.");
            return result;
        }
    }
    else if (LooksLikeGitHubSlug(repository))
    {
        repository = "github:" + repository;
    }

    result.alias = RepositoryAlias(repository);
    result.repository = repository;
    result.requirement = requirement;
    if (!IsSafePackageAlias(result.alias))
        AddError(result, "Package repository does not produce a safe dependency alias: " + result.alias);
    if (result.repository.empty())
        AddError(result, "Package repository cannot be empty.");
    return result;
}

bool ReplaceTextFileAtomically(const fs::path& destination,
                               std::string_view content,
                               std::string& error)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(destination, ec);
    if (ec)
    {
        error = "Cannot determine destination path: " + ec.message();
        return false;
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary = absolute;
    temporary += ".tmp-" + std::to_string(timestamp);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            error = "Cannot open temporary file: " + PathToUtf8(temporary);
            return false;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output.good())
        {
            error = "Cannot write temporary file: " + PathToUtf8(temporary);
            output.close();
            fs::remove(temporary, ec);
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), absolute.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Atomic replacement failed with Windows error " +
            std::to_string(GetLastError());
        fs::remove(temporary, ec);
        return false;
    }
#else
    fs::rename(temporary, absolute, ec);
    if (ec)
    {
        error = "Atomic replacement failed: " + ec.message();
        fs::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

std::vector<std::string> AddDependencyToManifest(
    const fs::path& packageRoot,
    const std::string& specification,
    bool development,
    const fs::path& standardLibraryPath)
{
    std::vector<std::string> errors;
    const AddDependencyResult parsed = ParseAddDependencySpec(specification);
    if (!parsed.IsValid())
        return parsed.errors;

    const Lode::Package::GitTagResolutionResult tag =
        Lode::Package::ResolveGitTag(parsed.repository, parsed.requirement);
    if (!tag.IsValid())
        return tag.errors;

    const fs::path manifestPath = packageRoot / "lode.json";
    nlohmann::json manifest;
    std::string originalManifest;
    try
    {
        std::ifstream file(manifestPath);
        if (!file.is_open())
        {
            errors.push_back("Cannot open package manifest: " + PathToUtf8(manifestPath));
            return errors;
        }
        originalManifest.assign(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
        manifest = nlohmann::json::parse(originalManifest);
    }
    catch (const std::exception& exception)
    {
        errors.push_back("Failed to parse package manifest: " + std::string(exception.what()));
        return errors;
    }

    if (!manifest.is_object())
    {
        errors.push_back("Package manifest must contain a JSON object.");
        return errors;
    }

    const char* fieldName = development ? "devDependencies" : "dependencies";
    const char* otherFieldName = development ? "dependencies" : "devDependencies";
    if (!manifest.contains(fieldName))
        manifest[fieldName] = nlohmann::json::object();
    if (!manifest[fieldName].is_object())
    {
        errors.push_back(std::string("lode.json.") + fieldName + " must be an object.");
        return errors;
    }
    if (manifest.contains(otherFieldName) && !manifest[otherFieldName].is_object())
    {
        errors.push_back(std::string("lode.json.") + otherFieldName + " must be an object.");
        return errors;
    }
    if (manifest.contains(otherFieldName) && manifest[otherFieldName].contains(parsed.alias))
    {
        errors.push_back("Dependency '" + parsed.alias +
            "' already exists in " + otherFieldName + ".");
        return errors;
    }
    if (manifest[fieldName].contains(parsed.alias))
    {
        errors.push_back("Dependency '" + parsed.alias +
            "' already exists in " + fieldName + ".");
        return errors;
    }

    const std::string requirement = parsed.requirement.empty() ? tag.version : parsed.requirement;
    manifest[fieldName][parsed.alias] = {
        { "git", parsed.repository },
        { "version", requirement }
    };

    std::string replacementError;
    const std::string content = manifest.dump(2) + "\n";
    if (!ReplaceTextFileAtomically(manifestPath, content, replacementError))
    {
        errors.push_back("Cannot update lode.json: " + replacementError);
        return errors;
    }

    const Lode::Package::InstallResult installation = Lode::Package::InstallLocal(
        packageRoot, standardLibraryPath, development);
    if (!installation.IsValid())
    {
        errors.insert(errors.end(), installation.errors.begin(), installation.errors.end());
        std::string restoreError;
        if (!ReplaceTextFileAtomically(manifestPath, originalManifest, restoreError))
            errors.push_back("Cannot restore lode.json after installation failure: " + restoreError);
        return errors;
    }
    return errors;
}

bool IsHelpArgument(std::string_view argument)
{
    return argument == "--help" || argument == "-h";
}

void PrintMainHelp()
{
    Lode::Logger::Info("Lode (lode) v1.0.0");
    Lode::Logger::Info("Usage: lode [--help] [--version]");
    Lode::Logger::Info("       lode <file.luac | file.luau> [script-arguments]");
    Lode::Logger::Info("       lode -c <code> [script-arguments]");
    Lode::Logger::Info("       lode <command> [options]");
    Lode::Logger::Info("Commands: init, add, install, pack, ci, help");
    Lode::Logger::Info("Run `lode <command> --help` for command-specific help.");
    Lode::Logger::Info("Run `lode` without arguments to start the interactive REPL.");
}

void PrintCommandHelp(std::string_view command)
{
    if (command == "init")
    {
        Lode::Logger::Info("Usage: lode init <name> --description <text> [--native] [--version <semver>] [--license <SPDX>] [project-root]");
        Lode::Logger::Info("Creates a pure Luau project by default; --native also creates a CMake native module.");
    }
    else if (command == "add")
    {
        Lode::Logger::Info("Usage: lode add [--dev] owner/repository[@version] [package-root]");
        Lode::Logger::Info("Adds a Git dependency and installs it into the package view.");
    }
    else if (command == "install")
    {
        Lode::Logger::Info("Usage: lode install [--locked] [--dev] [package-root]");
        Lode::Logger::Info("--locked requires lode.lock; --dev includes root development dependencies.");
    }
    else if (command == "pack")
    {
        Lode::Logger::Info("Usage: lode pack [--output <archive>] [package-root]");
        Lode::Logger::Info("Builds a validated package archive and its SHA-256 sidecar.");
    }
    else if (command == "ci")
    {
        Lode::Logger::Info("Usage: lode ci validate [--source|--artifact] [--locked] [package-root]");
        Lode::Logger::Info("       lode ci init [--force] --sdk-version <nightly> --sdk-sha256 <sha256> [package-root]");
        Lode::Logger::Info("       lode ci update [package-root]");
    }
    else if (command == "-c")
    {
        Lode::Logger::Info("Usage: lode -c <code> [script-arguments]");
        Lode::Logger::Info("Executes one quoted Luau source string. The string may contain newlines.");
    }
    else
    {
        PrintMainHelp();
    }
}

void EmitExecutionError(std::string_view error, std::string_view sourceName)
{
    Lode::Diagnostic diagnostic = Lode::Logger::ParseLuauError(error, sourceName);
    diagnostic.code = "RuntimeError";
    Lode::Logger::EmitDiagnostic(diagnostic);
}

bool CompileSource(std::string_view source, std::string_view sourceName, std::string& bytecode)
{
    std::vector<Lode::Diagnostic> diagnostics;
    bytecode = Lode::Compiler::CompileWithResult(source, diagnostics, nullptr, sourceName);
    bool hasErrors = false;
    for (const Lode::Diagnostic& diagnostic : diagnostics)
    {
        Lode::Logger::EmitDiagnostic(diagnostic);
        hasErrors = hasErrors || !diagnostic.isWarning;
    }
    return !hasErrors && !bytecode.empty();
}

Lode::Result<Lode::State> CreateCliState(const fs::path& standardLibraryPath,
                                         const std::vector<std::string>& cliArgs)
{
    auto stateResult = Lode::State::Create();
    if (stateResult.IsError())
    {
        Lode::Diagnostic diagnostic;
        diagnostic.message = "Error initializing runtime state: " + stateResult.GetError().ErrorMessage();
        diagnostic.code = "VMInitError";
        Lode::Logger::EmitDiagnostic(diagnostic);
        return stateResult.GetError();
    }

    Lode::State vm = std::move(stateResult.GetValue());
    if (!standardLibraryPath.empty())
        vm.SetStandardLibraryPath(PathToUtf8(standardLibraryPath));
    vm.SetCliArgs(cliArgs);
    return std::move(vm);
}

int RunCommandCode(std::string_view source,
                   const fs::path& standardLibraryPath,
                   const std::vector<std::string>& scriptArgs)
{
    std::string bytecode;
    constexpr std::string_view sourceName = "=(command line)";
    if (!CompileSource(source, sourceName, bytecode))
        return 1;

    auto stateResult = CreateCliState(standardLibraryPath, scriptArgs);
    if (stateResult.IsError())
        return 1;
    Lode::State vm = std::move(stateResult.GetValue());

    auto execution = vm.ExecuteBytecode(bytecode, sourceName);
    if (execution.IsError())
    {
        EmitExecutionError(execution.GetError().ErrorMessage(), sourceName);
        return 1;
    }
    vm.GetEventLoop().Run(vm);
    const std::string mainError = Lode::Task::GetMainThreadError(vm);
    if (!mainError.empty())
    {
        EmitExecutionError(mainError, sourceName);
        return 1;
    }
    return 0;
}

std::string FormatReplValue(const Lode::Value& value)
{
    switch (value.GetType())
    {
    case Lode::ValueType::Nil: return "nil";
    case Lode::ValueType::Boolean: return value.AsBoolean() ? "true" : "false";
    case Lode::ValueType::Integer: return std::to_string(value.AsInteger());
    case Lode::ValueType::Number:
    {
        std::ostringstream output;
        output << std::setprecision(15) << value.AsNumber();
        return output.str();
    }
    case Lode::ValueType::String: return value.AsString();
    case Lode::ValueType::Vector: return "<vector>";
    case Lode::ValueType::Table: return "<table>";
    case Lode::ValueType::Function: return "<function>";
    case Lode::ValueType::Thread: return "<thread>";
    case Lode::ValueType::Userdata: return "<userdata>";
    case Lode::ValueType::LightUserdata: return "<lightuserdata>";
    case Lode::ValueType::Buffer: return "<buffer>";
    }
    return "<value>";
}

int RunRepl(const fs::path& standardLibraryPath)
{
    auto stateResult = CreateCliState(standardLibraryPath, {});
    if (stateResult.IsError())
        return 1;
    Lode::State vm = std::move(stateResult.GetValue());

    std::cout << "Lode REPL v1.0.0\n"
              << "Enter Luau code, .help for commands, or .exit to quit.\n";
    std::string line;
    while (std::cout << "lode> " && std::getline(std::cin, line))
    {
        if (line == ".exit" || line == ".quit")
            break;
        if (line == ".help")
        {
            std::cout << ".exit, .quit  Leave the REPL\n"
                      << "End a line with \\ to continue it on the next prompt.\n";
            continue;
        }
        if (line.empty())
            continue;

        std::string source = line;
        while (!source.empty() && source.back() == '\\')
        {
            source.pop_back();
            std::string continuation;
            if (!(std::cout << "... " && std::getline(std::cin, continuation)))
                return 0;
            source += '\n' + continuation;
        }

        std::string expressionBytecode;
        std::vector<Lode::Diagnostic> expressionDiagnostics;
        expressionBytecode = Lode::Compiler::CompileWithResult(
            "return " + source, expressionDiagnostics, nullptr, "=(repl)");
        const bool expressionIsValid = expressionBytecode.size() > 0 &&
            std::none_of(expressionDiagnostics.begin(), expressionDiagnostics.end(),
                [](const Lode::Diagnostic& diagnostic) { return !diagnostic.isWarning; });
        if (expressionIsValid)
        {
            auto result = vm.ProtectedCall(expressionBytecode, "=(repl)");
            if (result.IsError())
                EmitExecutionError(result.GetError().ErrorMessage(), "=(repl)");
            else if (!result.GetValue().IsNil())
                std::cout << FormatReplValue(result.GetValue()) << '\n';
        }
        else
        {
            std::string bytecode;
            if (!CompileSource(source, "=(repl)", bytecode))
                continue;
            auto result = vm.ExecuteBytecodeWithResults(bytecode, "=(repl)", true);
            if (result.IsError())
                EmitExecutionError(result.GetError().ErrorMessage(), "=(repl)");
            else if (result.GetValue() > 0)
                vm.Pop(result.GetValue());
        }

        vm.GetEventLoop().Run(vm);
        const std::string mainError = Lode::Task::GetMainThreadError(vm);
        if (!mainError.empty())
            EmitExecutionError(mainError, "=(repl)");
    }
    return 0;
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
        return RunRepl(standardLibraryPath);

    const std::string firstArgument = PathToUtf8(fs::path(argv[1]));
    if (firstArgument == "--version" || firstArgument == "-V")
    {
        Lode::Logger::Info("Lode (lode) v1.0.0");
        return 0;
    }
    if (IsHelpArgument(firstArgument))
    {
        PrintMainHelp();
        return 0;
    }
    if (firstArgument == "help")
    {
        if (argc == 2)
            PrintMainHelp();
        else if (PathToUtf8(fs::path(argv[2])) == "ci" && argc > 3)
            PrintCommandHelp("ci");
        else
            PrintCommandHelp(PathToUtf8(fs::path(argv[2])));
        return 0;
    }
    if (firstArgument == "-c")
    {
        if (argc < 3 || IsHelpArgument(argc >= 3 ? PathToUtf8(fs::path(argv[2])) : ""))
        {
            PrintCommandHelp("-c");
            return argc < 3 ? 1 : 0;
        }
        std::vector<std::string> scriptArgs;
        for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
            scriptArgs.push_back(PathToUtf8(fs::path(argv[argumentIndex])));
        return RunCommandCode(PathToUtf8(fs::path(argv[2])), standardLibraryPath, scriptArgs);
    }
    if (firstArgument == "init")
    {
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            if (IsHelpArgument(PathToUtf8(fs::path(argv[argumentIndex]))))
            {
                PrintCommandHelp("init");
                return 0;
            }
        }
        Lode::Package::ProjectInitOptions options;
        fs::path projectRoot = fs::current_path();
        bool hasProjectRoot = false;
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
            if (argument == "--native")
            {
                options.native = true;
            }
            else if (argument == "--description" || argument == "--version" || argument == "--license")
            {
                if (argumentIndex + 1 >= argc)
                {
                    Lode::Logger::Error("Usage: lode init <name> --description <text> [--native] [--version <semver>] [--license <id>] [project-root]");
                    PrintCommandHelp("init");
                    return 1;
                }
                const std::string value = PathToUtf8(fs::path(argv[++argumentIndex]));
                if (argument == "--description") options.description = value;
                else if (argument == "--version") options.version = value;
                else options.license = value;
            }
            else if (argument.rfind("--", 0) == 0)
            {
                Lode::Logger::Error("Usage: lode init <name> --description <text> [--native] [--version <semver>] [--license <id>] [project-root]");
                PrintCommandHelp("init");
                return 1;
            }
            else if (options.name.empty())
            {
                options.name = argument;
            }
            else if (!hasProjectRoot)
            {
                projectRoot = fs::path(argv[argumentIndex]);
                hasProjectRoot = true;
            }
            else
            {
                Lode::Logger::Error("Usage: lode init <name> --description <text> [--native] [--version <semver>] [--license <id>] [project-root]");
                PrintCommandHelp("init");
                return 1;
            }
        }
        const Lode::Package::ProjectInitResult result = Lode::Package::InitializeProject(projectRoot, options);
        for (const std::string& error : result.errors)
            Lode::Logger::Error(error);
        if (!result.IsValid())
            return 1;
        Lode::Logger::Success("Initialized " + std::string(options.native ? "native" : "Luau") +
            " project: " + PathToUtf8(fs::absolute(projectRoot)));
        return 0;
    }

    if (firstArgument == "add")
    {
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            if (IsHelpArgument(PathToUtf8(fs::path(argv[argumentIndex]))))
            {
                PrintCommandHelp("add");
                return 0;
            }
        }
        bool development = false;
        fs::path packageRoot = fs::current_path();
        std::string specification;
        bool hasPackageRoot = false;
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
            if (argument == "--dev")
            {
                development = true;
            }
            else if (argument.rfind("--", 0) == 0)
            {
                Lode::Logger::Error("Usage: lode add [--dev] owner/repository[@version] [package-root]");
                PrintCommandHelp("add");
                return 1;
            }
            else if (specification.empty())
            {
                specification = argument;
            }
            else if (!hasPackageRoot)
            {
                packageRoot = fs::path(argument);
                hasPackageRoot = true;
            }
            else
            {
                Lode::Logger::Error("Usage: lode add [--dev] owner/repository[@version] [package-root]");
                PrintCommandHelp("add");
                return 1;
            }
        }

        if (specification.empty())
        {
            Lode::Logger::Error("Usage: lode add [--dev] owner/repository[@version] [package-root]");
            PrintCommandHelp("add");
            return 1;
        }

        const std::vector<std::string> errors = AddDependencyToManifest(
            packageRoot, specification, development, standardLibraryPath);
        for (const std::string& error : errors)
            Lode::Logger::Error(error);
        if (!errors.empty())
            return 1;

        Lode::Logger::Success("Added and installed dependency from " + specification + ".");
        return 0;
    }

    if (firstArgument == "install")
    {
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            if (IsHelpArgument(PathToUtf8(fs::path(argv[argumentIndex]))))
            {
                PrintCommandHelp("install");
                return 0;
            }
        }
        bool locked = false;
        bool includeDevelopmentDependencies = false;
        fs::path packageRoot = fs::current_path();
        bool hasPackageRoot = false;
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
            if (argument == "--locked")
            {
                locked = true;
            }
            else if (argument == "--dev")
            {
                includeDevelopmentDependencies = true;
            }
            else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
            {
                Lode::Logger::Error("Usage: lode install [--locked] [--dev] [package-root]");
                PrintCommandHelp("install");
                return 1;
            }
            else
            {
                packageRoot = fs::path(argv[argumentIndex]);
                hasPackageRoot = true;
            }
        }

        Lode::Package::InstallResult result = locked
            ? Lode::Package::InstallLocked(
                packageRoot, standardLibraryPath, includeDevelopmentDependencies)
            : Lode::Package::InstallLocal(
                packageRoot, standardLibraryPath, includeDevelopmentDependencies);
        for (const std::string& error : result.errors)
            Lode::Logger::Error(error);
        if (!result.IsValid())
            return 1;

        Lode::Logger::Success(std::string(locked ? "Locked" : "Local") +
            " package installation completed: " +
            PathToUtf8(fs::absolute(packageRoot)));
        return 0;
    }

    if (firstArgument == "pack")
    {
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            if (IsHelpArgument(PathToUtf8(fs::path(argv[argumentIndex]))))
            {
                PrintCommandHelp("pack");
                return 0;
            }
        }
        fs::path packageRoot = fs::current_path();
        fs::path outputArchive;
        bool hasPackageRoot = false;
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
            if (argument == "--output")
            {
                if (argumentIndex + 1 >= argc)
                {
                    Lode::Logger::Error("Usage: lode pack [--output <archive>] [package-root]");
                    PrintCommandHelp("pack");
                    return 1;
                }
                outputArchive = fs::path(argv[++argumentIndex]);
            }
            else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
            {
                Lode::Logger::Error("Usage: lode pack [--output <archive>] [package-root]");
                PrintCommandHelp("pack");
                return 1;
            }
            else
            {
                packageRoot = fs::path(argv[argumentIndex]);
                hasPackageRoot = true;
            }
        }

        const Lode::Package::PackResult result = Lode::Package::PackPackage(
            packageRoot, standardLibraryPath, outputArchive);
        for (const std::string& error : result.errors)
            Lode::Logger::Error(error);
        if (!result.IsValid())
            return 1;

        Lode::Logger::Success("Package archive created: " +
            PathToUtf8(result.archivePath));
        Lode::Logger::Success("Package checksum created: " +
            PathToUtf8(result.checksumPath));
        return 0;
    }

    if (firstArgument == "ci")
    {
        if (argc < 3)
        {
            PrintCommandHelp("ci");
            return 1;
        }

        const std::string ciCommand = PathToUtf8(fs::path(argv[2]));
        if (IsHelpArgument(ciCommand))
        {
            PrintCommandHelp("ci");
            return 0;
        }
        for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
        {
            if (IsHelpArgument(PathToUtf8(fs::path(argv[argumentIndex]))))
            {
                PrintCommandHelp("ci");
                return 0;
            }
        }
        if (ciCommand == "init")
        {
            bool force = false;
            Lode::Package::CiSdkPin sdkPin;
            fs::path packageRoot = fs::current_path();
            bool hasPackageRoot = false;
            for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
            {
                const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
                if (argument == "--force")
                {
                    force = true;
                }
                else if (argument == "--sdk-version" || argument == "--sdk-sha256")
                {
                    if (argumentIndex + 1 >= argc)
                    {
                        Lode::Logger::Error("Usage: lode ci init [--force] --sdk-version <nightly> --sdk-sha256 <sha256> [package-root]");
                        PrintCommandHelp("ci");
                        return 1;
                    }
                    const std::string value = PathToUtf8(fs::path(argv[++argumentIndex]));
                    if (argument == "--sdk-version")
                        sdkPin.version = value;
                    else
                        sdkPin.sha256 = value;
                }
                else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
                {
                    Lode::Logger::Error("Usage: lode ci init [--force] --sdk-version <nightly> --sdk-sha256 <sha256> [package-root]");
                    PrintCommandHelp("ci");
                    return 1;
                }
                else
                {
                    packageRoot = fs::path(argv[argumentIndex]);
                    hasPackageRoot = true;
                }
            }

            Lode::Package::ValidationReport report = Lode::Package::GenerateWorkflow(
                packageRoot, force, sdkPin, standardLibraryPath);
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
                PrintCommandHelp("ci");
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
            Lode::Logger::Error("Unknown lode ci command: " + ciCommand);
            PrintCommandHelp("ci");
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
                PrintCommandHelp("ci");
                return 1;
            }
            else
            {
                packageRoot = fs::path(argv[argumentIndex]);
                hasPackageRoot = true;
            }
        }

        Lode::Package::ValidationReport report;
        if (locked && (mode == Lode::Package::ValidationMode::Source ||
                       mode == Lode::Package::ValidationMode::Artifact))
        {
            // A locked install may select a standard-module artifact that is
            // not present in the bundled catalog. Reuse the exact locked
            // graph validation instead of resolving only against that catalog.
            const Lode::Package::ValidationMode lockedMode =
                mode == Lode::Package::ValidationMode::Artifact
                    ? Lode::Package::ValidationMode::LockedArtifact
                    : Lode::Package::ValidationMode::InstallSource;
            report = Lode::Package::ValidateLockedPackage(
                packageRoot, standardLibraryPath, true, lockedMode);
        }
        else
        {
            report = Lode::Package::Validate(
                packageRoot, mode, standardLibraryPath);
        }
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
