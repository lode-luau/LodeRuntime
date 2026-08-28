// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Result.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Gc.hpp"
#include "Lode/Task.hpp"
#include "CiGenerator.hpp"
#include "GitResolver.hpp"
#include "PackageValidator.hpp"
#include "PackageManifest.hpp"
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
#include <optional>
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

bool IsStdlibModuleName(const std::string& name)
{
    if (name.empty())
        return false;
    for (const char character : name)
    {
        if (character == '/' || character == '\\' || character == ':' || character == '.')
            return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' || character == '_';
    });
}

std::string FindStdlibModuleVersion(const fs::path& standardLibraryPath,
                                    const std::string& moduleName)
{
    if (standardLibraryPath.empty())
        return {};

    const fs::path manifestPath = standardLibraryPath / "modules" / moduleName / "package.luau";
    if (!fs::is_regular_file(manifestPath))
        return {};

    const Lode::Package::PackageManifestResult parsed =
        Lode::Package::ReadPackageManifest(manifestPath);
    if (!parsed.IsValid())
        return {};

    return parsed.document.value("version", "");
}

// Walks up from the package root looking for a standard library catalog
// (a directory with .config.luau and a modules/ subdirectory but no
// package.luau at its own root).
fs::path FindStdlibRootFromPackage(const fs::path& packageRoot)
{
    std::error_code ec;
    fs::path current = fs::weakly_canonical(fs::absolute(packageRoot, ec), ec);
    while (!current.empty())
    {
        const fs::path siblingModules = current / "modules";
        if (fs::is_directory(siblingModules, ec) &&
            fs::is_regular_file(current / ".config.luau", ec) &&
            !fs::is_regular_file(current / "package.luau", ec))
            return current;

        const fs::path parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }
    return {};
}

// Resolves the effective standard library path, trying the installed catalog
// first and falling back to a source-tree walk from the package root.
fs::path ResolveEffectiveStdlibPath(const fs::path& standardLibraryPath,
                                    const fs::path& packageRoot)
{
    if (!standardLibraryPath.empty())
        return standardLibraryPath;
    return FindStdlibRootFromPackage(packageRoot);
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
    {
        bool moved = MoveFileExW(temporary.c_str(), absolute.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

        // MoveFileExW can fail with ERROR_ACCESS_DENIED (5) when the target
        // has readonly/hidden attributes or the process lacks DELETE permission.
        // ERROR_ALREADY_EXISTS (183) can occur when a directory exists at the
        // destination path.
        const DWORD lastError = GetLastError();
        if (!moved && (lastError == ERROR_ACCESS_DENIED || lastError == ERROR_ALREADY_EXISTS))
        {
            SetFileAttributesW(absolute.c_str(), FILE_ATTRIBUTE_NORMAL);
            moved = MoveFileExW(temporary.c_str(), absolute.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!moved)
            {
                std::error_code removeEc;
                if (fs::is_directory(absolute, removeEc))
                    fs::remove_all(absolute, removeEc);
                else
                    fs::remove(absolute, removeEc);
                moved = MoveFileW(temporary.c_str(), absolute.c_str()) != 0;
            }
        }

        if (!moved)
        {
            error = "Atomic replacement failed with Windows error " +
                std::to_string(GetLastError());
            fs::remove(temporary, ec);
            return false;
        }
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

    const fs::path manifestPath = packageRoot / "package.luau";
    nlohmann::json manifest;
    std::string originalManifest;
    try
    {
        std::ifstream file(manifestPath);
        if (file.is_open())
        {
            originalManifest.assign(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>());
            const Lode::Package::PackageManifestResult manifestResult =
                Lode::Package::ReadPackageManifest(manifestPath);
            if (!manifestResult.IsValid())
            {
                errors.insert(errors.end(), manifestResult.errors.begin(), manifestResult.errors.end());
                return errors;
            }
            manifest = manifestResult.document;
        }
        else
        {
            // No manifest exists yet: bootstrap a minimal one so that
            // `lode add` works from a bare directory.
            manifest = nlohmann::json::object();
            manifest["name"] = packageRoot.filename().string();
            manifest["version"] = "0.1.0";

            // Ensure init.luau exists so that InstallLocal passes validation.
            const fs::path initPath = packageRoot / "init.luau";
            if (!fs::is_regular_file(initPath))
            {
                std::ofstream initFile(initPath, std::ios::binary | std::ios::trunc);
                initFile << "return {}\n";
            }
        }
    }
    catch (const std::exception& exception)
    {
        errors.push_back("Failed to parse package manifest: " + std::string(exception.what()));
        return errors;
    }

    if (!manifest.is_object())
    {
        errors.push_back("Package manifest must contain a Luau table.");
        return errors;
    }

    const char* fieldName = development ? "devDependencies" : "dependencies";
    const char* otherFieldName = development ? "dependencies" : "devDependencies";
    if (!manifest.contains(fieldName))
        manifest[fieldName] = nlohmann::json::object();
    if (!manifest[fieldName].is_object())
    {
        errors.push_back(std::string("package manifest.") + fieldName + " must be an object.");
        return errors;
    }
    if (manifest.contains(otherFieldName) && !manifest[otherFieldName].is_object())
    {
        errors.push_back(std::string("package manifest.") + otherFieldName + " must be an object.");
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

    // Check if this is a bare standard library module name (e.g. "task", "http").
    // Stdlib modules use a plain version string in the manifest instead of a
    // { git = ..., version = ... } table.
    const fs::path effectiveStdlibPath = ResolveEffectiveStdlibPath(standardLibraryPath, packageRoot);
    // A local path or Git URL can have a simple basename (for example,
    // `tests/package/lode-git-add-source`).  Only a bare specification should
    // be interpreted as an official standard-library module.
    if (IsStdlibModuleName(parsed.alias) && parsed.repository == parsed.alias)
    {
        if (effectiveStdlibPath.empty())
        {
            errors.push_back("Standard library module '" + parsed.alias +
                "' requires a stdlib catalog. Run `lode init` from a project inside the Lode source tree.");
            return errors;
        }

        const std::string resolvedVersion = FindStdlibModuleVersion(
            effectiveStdlibPath, parsed.alias);
        if (resolvedVersion.empty())
        {
            errors.push_back("Standard library module '" + parsed.alias +
                "' was not found in the stdlib catalog at " +
                PathToUtf8(effectiveStdlibPath) + ".");
            return errors;
        }

        const std::string requirement = parsed.requirement.empty()
            ? resolvedVersion : parsed.requirement;
        manifest[fieldName][parsed.alias] = requirement;

        std::string replacementError;
        const std::string content = Lode::Package::SerializePackageManifest(manifest);
        if (content.empty())
        {
            errors.push_back("Cannot serialize package manifest.");
            return errors;
        }
        if (!ReplaceTextFileAtomically(manifestPath, content, replacementError))
        {
            errors.push_back("Cannot update package manifest: " + replacementError);
            return errors;
        }

        const Lode::Package::InstallResult installation = Lode::Package::InstallLocal(
            packageRoot, effectiveStdlibPath, development);
        if (!installation.IsValid())
        {
            errors.insert(errors.end(), installation.errors.begin(), installation.errors.end());
            std::string restoreError;
            if (!ReplaceTextFileAtomically(manifestPath, originalManifest, restoreError))
                errors.push_back("Cannot restore package manifest after installation failure: " + restoreError);
            return errors;
        }
        return errors;
    }

    // Git dependency path: resolve the remote tag before writing the manifest.
    const Lode::Package::GitTagResolutionResult tag =
        Lode::Package::ResolveGitTag(parsed.repository, parsed.requirement);
    if (!tag.IsValid())
        return tag.errors;

    const std::string requirement = parsed.requirement.empty() ? tag.version : parsed.requirement;
    manifest[fieldName][parsed.alias] = {
        { "git", parsed.repository },
        { "version", requirement }
    };

    std::string replacementError;
    const std::string content = Lode::Package::SerializePackageManifest(manifest);
    if (content.empty())
    {
        errors.push_back("Cannot serialize package manifest.");
        return errors;
    }
    if (!ReplaceTextFileAtomically(manifestPath, content, replacementError))
    {
        errors.push_back("Cannot update package manifest: " + replacementError);
        return errors;
    }

    const Lode::Package::InstallResult installation = Lode::Package::InstallLocal(
        packageRoot, standardLibraryPath, development);
    if (!installation.IsValid())
    {
        errors.insert(errors.end(), installation.errors.begin(), installation.errors.end());
        std::string restoreError;
        if (!ReplaceTextFileAtomically(manifestPath, originalManifest, restoreError))
            errors.push_back("Cannot restore package manifest after installation failure: " + restoreError);
        return errors;
    }
    return errors;
}

bool IsHelpArgument(std::string_view argument)
{
    return argument == "--help" || argument == "-h";
}

// Performance tuning flags accepted before the first script/command argument.
// Every field is optional: anything left unset keeps the previous behavior,
// so passing none of these flags changes nothing.
struct PerfSettings
{
    std::optional<int> optLevel;                  // --opt=N: compiler optimization level override
    std::optional<Lode::CodeGenMode> codegenMode; // --codegen=native|all|off
    std::optional<int> gcGoal;                    // --gc-goal=PCT
    std::optional<int> gcStepmul;                 // --gc-stepmul=PCT
    std::optional<int> gcStepsize;                // --gc-stepsize=KB
    std::optional<double> memLimitMb;             // --mem-limit=MB (soft limit)
};

std::optional<int> ParseFlagInt(std::string_view value)
{
    if (value.empty() || value.size() > 9)
        return std::nullopt;
    int result = 0;
    for (const char character : value)
    {
        if (character < '0' || character > '9')
            return std::nullopt;
        result = result * 10 + (character - '0');
    }
    return result;
}

std::optional<double> ParseFlagNumber(std::string_view value)
{
    if (value.empty())
        return std::nullopt;
    double result = 0.0;
    double fraction = 0.1;
    bool sawDigit = false;
    bool sawDot = false;
    for (const char character : value)
    {
        if (character >= '0' && character <= '9')
        {
            sawDigit = true;
            if (sawDot)
            {
                result += static_cast<double>(character - '0') * fraction;
                fraction *= 0.1;
            }
            else
            {
                result = result * 10.0 + static_cast<double>(character - '0');
            }
        }
        else if (character == '.' && !sawDot)
        {
            sawDot = true;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!sawDigit)
        return std::nullopt;
    return result;
}

// Recognizes one performance tuning flag. Returns:
//  0 -> not a performance flag (the argument is left untouched),
//  1 -> parsed and stored into `settings`,
// -1 -> recognized but invalid (a friendly error was already logged).
int ConsumePerfFlag(const std::string& argument, PerfSettings& settings)
{
    if (argument.rfind("--", 0) != 0)
        return 0;

    const size_t equals = argument.find('=');
    const std::string name = equals == std::string::npos ? argument : argument.substr(0, equals);
    const std::string value = equals == std::string::npos ? std::string() : argument.substr(equals + 1);

    if (name == "--opt")
    {
        const std::optional<int> parsed = ParseFlagInt(value);
        if (!parsed || *parsed > 2)
        {
            Lode::Logger::Error("Invalid --opt value '" + value + "'. Expected an integer between 0 and 2.");
            return -1;
        }
        settings.optLevel = *parsed;
        return 1;
    }
    if (name == "--codegen")
    {
        if (value == "native")
            settings.codegenMode = Lode::CodeGenMode::NativeModulesOnly;
        else if (value == "all")
            settings.codegenMode = Lode::CodeGenMode::AllFunctions;
        else if (value == "off")
            settings.codegenMode = Lode::CodeGenMode::Off;
        else
        {
            Lode::Logger::Error("Invalid --codegen value '" + value + "'. Expected native, all, or off.");
            return -1;
        }
        return 1;
    }
    if (name == "--gc-goal")
    {
        const std::optional<int> parsed = ParseFlagInt(value);
        if (!parsed || *parsed <= 0)
        {
            Lode::Logger::Error("Invalid --gc-goal value '" + value + "'. Expected a positive percentage (for example 300).");
            return -1;
        }
        settings.gcGoal = *parsed;
        return 1;
    }
    if (name == "--gc-stepmul")
    {
        const std::optional<int> parsed = ParseFlagInt(value);
        if (!parsed || *parsed <= 0)
        {
            Lode::Logger::Error("Invalid --gc-stepmul value '" + value + "'. Expected a positive percentage (for example 200).");
            return -1;
        }
        settings.gcStepmul = *parsed;
        return 1;
    }
    if (name == "--gc-stepsize")
    {
        const std::optional<int> parsed = ParseFlagInt(value);
        if (!parsed || *parsed <= 0)
        {
            Lode::Logger::Error("Invalid --gc-stepsize value '" + value + "'. Expected a positive size in kilobytes.");
            return -1;
        }
        settings.gcStepsize = *parsed;
        return 1;
    }
    if (name == "--mem-limit")
    {
        const std::optional<double> parsed = ParseFlagNumber(value);
        if (!parsed || *parsed <= 0.0)
        {
            Lode::Logger::Error("Invalid --mem-limit value '" + value + "'. Expected a positive size in megabytes.");
            return -1;
        }
        settings.memLimitMb = *parsed;
        return 1;
    }
    return 0;
}

// Scans `arguments` from beginIndex and consumes every performance flag found
// there, stopping at the first non-flag argument. Returns false after logging
// a friendly error when any flag value is invalid; otherwise firstUnconsumed
// receives the index of the first argument that was not consumed.
bool ParsePerfArguments(const std::vector<std::string>& arguments, int beginIndex,
                        PerfSettings& settings, int& firstUnconsumed)
{
    int index = beginIndex;
    while (index < static_cast<int>(arguments.size()))
    {
        const int status = ConsumePerfFlag(arguments[static_cast<size_t>(index)], settings);
        if (status < 0)
            return false;
        if (status == 0)
            break;
        ++index;
    }
    firstUnconsumed = index;
    return true;
}

// Applies the parsed performance flags to a freshly created VM state. Flags
// that were not given leave the corresponding setting at its default.
void ApplyPerfSettings(Lode::State& vm, const PerfSettings& perf)
{
    if (perf.codegenMode)
        vm.SetCodeGenMode(*perf.codegenMode);
    if (perf.gcGoal)
        Lode::Gc::SetGoal(vm, *perf.gcGoal);
    if (perf.gcStepmul)
        Lode::Gc::SetStepMultiplier(vm, *perf.gcStepmul);
    if (perf.gcStepsize)
        Lode::Gc::SetStepSize(vm, *perf.gcStepsize);
    if (perf.memLimitMb)
    {
        Lode::GcBudget budget;
        // Without an explicit --gc-stepsize, 0 lets Luau pick its automatic step size.
        budget.stepSizeKB = perf.gcStepsize.value_or(0);
        budget.softLimitKB = *perf.memLimitMb * 1024.0;
        vm.GetEventLoop().SetGcBudget(budget);
    }
}

void PrintMainHelp()
{
    Lode::Logger::Info("Lode (lode) v1.0.0");
    Lode::Logger::Info("Usage: lode [--help] [--version]");
    Lode::Logger::Info("       lode <file.luac | file.luau> [script-arguments]");
    Lode::Logger::Info("       lode -c <code> [script-arguments]");
    Lode::Logger::Info("       lode <command> [options]");
    Lode::Logger::Info("Commands: init, add, install, pack, ci, help");
    Lode::Logger::Info("Performance flags: --opt=<0-2> --codegen=native|all|off --gc-goal=<pct> --gc-stepmul=<pct> --gc-stepsize=<kb> --mem-limit=<mb>");
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
        Lode::Logger::Info("Usage: lode add [--dev] <module|owner/repo>[@version] ... [package-root]");
        Lode::Logger::Info("       lode add [--dev] task http json");
        Lode::Logger::Info("Adds Git or standard library dependencies and installs them.");
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
        Lode::Logger::Info("       lode ci init [--force] --lode-version <nightly> --lode-sha256 <sha256> [package-root]");
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

bool CompileSource(std::string_view source, std::string_view sourceName, std::string& bytecode,
                   const PerfSettings& perf)
{
    std::vector<Lode::Diagnostic> diagnostics;
    // Resolve the options from source hotcomments so behavior matches the
    // cached script path, then apply the --opt override when given.
    lua_CompileOptions options = Lode::Compiler::ParseOptionsFromSource(source, sourceName);
    if (perf.optLevel)
        options.optimizationLevel = *perf.optLevel;
    bytecode = Lode::Compiler::CompileWithResult(source, diagnostics, &options, sourceName);
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
                   const std::vector<std::string>& scriptArgs,
                   const PerfSettings& perf)
{
    std::string bytecode;
    constexpr std::string_view sourceName = "=(command line)";
    if (!CompileSource(source, sourceName, bytecode, perf))
        return 1;

    auto stateResult = CreateCliState(standardLibraryPath, scriptArgs);
    if (stateResult.IsError())
        return 1;
    Lode::State vm = std::move(stateResult.GetValue());
    ApplyPerfSettings(vm, perf);

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

int RunRepl(const fs::path& standardLibraryPath, const PerfSettings& perf)
{
    auto stateResult = CreateCliState(standardLibraryPath, {});
    if (stateResult.IsError())
        return 1;
    Lode::State vm = std::move(stateResult.GetValue());
    ApplyPerfSettings(vm, perf);

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
            if (!CompileSource(source, "=(repl)", bytecode, perf))
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

    // Performance tuning flags (--opt/--codegen/--gc-*/--mem-limit) are
    // consumed from any position before the first script/command argument so
    // they never leak into script arguments. Remaining arguments rotate into
    // argv[1..], keeping every dispatch branch below unchanged.
    PerfSettings perf;
    int firstUnconsumed = 1;
    if (argc >= 2)
    {
        std::vector<std::string> utf8Arguments;
        utf8Arguments.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i)
            utf8Arguments.push_back(PathToUtf8(fs::path(argv[i])));
        if (!ParsePerfArguments(utf8Arguments, 1, perf, firstUnconsumed))
            return 1;

        const int consumed = firstUnconsumed - 1;
        if (consumed > 0)
        {
            for (int i = 1; i + consumed < argc; ++i)
                argv[i] = argv[i + consumed];
            argc -= consumed;
        }
    }

    if (argc < 2)
        return RunRepl(standardLibraryPath, perf);

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
        return RunCommandCode(PathToUtf8(fs::path(argv[2])), standardLibraryPath, scriptArgs, perf);
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
        std::vector<std::string> specifications;
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
                Lode::Logger::Error("Usage: lode add [--dev] <module|owner/repo>[@version] ... [package-root]");
                PrintCommandHelp("add");
                return 1;
            }
            else
            {
                // Collect all remaining non-flag arguments.  The last one is
                // the package root only if it looks like an existing directory
                // and there are other specifications before it.
                if (!hasPackageRoot && !specifications.empty() &&
                    fs::is_directory(fs::path(argument)))
                {
                    packageRoot = fs::path(argument);
                    hasPackageRoot = true;
                }
                else
                {
                    specifications.push_back(argument);
                }
            }
        }

        if (specifications.empty())
        {
            Lode::Logger::Error("Usage: lode add [--dev] <module|owner/repo>[@version] ... [package-root]");
            PrintCommandHelp("add");
            return 1;
        }

        bool anyFailed = false;
        for (const std::string& specification : specifications)
        {
            const std::vector<std::string> errors = AddDependencyToManifest(
                packageRoot, specification, development, standardLibraryPath);
            for (const std::string& error : errors)
                Lode::Logger::Error(error);
            if (!errors.empty())
                anyFailed = true;
            else
                Lode::Logger::Success("Added and installed dependency: " + specification + ".");
        }
        if (anyFailed)
            return 1;
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
            Lode::Package::CiLodePin lodePin;
            fs::path packageRoot = fs::current_path();
            bool hasPackageRoot = false;
            for (int argumentIndex = 3; argumentIndex < argc; ++argumentIndex)
            {
                const std::string argument = PathToUtf8(fs::path(argv[argumentIndex]));
                if (argument == "--force")
                {
                    force = true;
                }
                else if (argument == "--lode-version" || argument == "--lode-sha256")
                {
                    if (argumentIndex + 1 >= argc)
                    {
                        Lode::Logger::Error("Usage: lode ci init [--force] --lode-version <nightly> --lode-sha256 <sha256> [package-root]");
                        PrintCommandHelp("ci");
                        return 1;
                    }
                    const std::string value = PathToUtf8(fs::path(argv[++argumentIndex]));
                    if (argument == "--lode-version")
                        lodePin.version = value;
                    else
                        lodePin.sha256 = value;
                }
                else if (argument.rfind("--", 0) == 0 || hasPackageRoot)
                {
                    Lode::Logger::Error("Usage: lode ci init [--force] --lode-version <nightly> --lode-sha256 <sha256> [package-root]");
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
                packageRoot, force, lodePin, standardLibraryPath);
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
        lua_CompileOptions options = Lode::Compiler::ParseOptionsFromSource(content, filePathUtf8);
        if (perf.optLevel)
            options.optimizationLevel = *perf.optLevel;
        bytecode = Lode::Compiler::CompileWithCache(content, filePathUtf8, &options, &diagnostics);

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

    ApplyPerfSettings(vm, perf);

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
