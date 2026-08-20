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
        Lode::Logger::Info("       lode install [--locked] [--dev] [package-root]");
        Lode::Logger::Info("       lode add [--dev] owner/repository[@version] [package-root]");
        Lode::Logger::Info("       lode ci validate [--source|--artifact] [--locked] [package-root]");
        Lode::Logger::Info("       lode ci init [--force] [package-root]");
        Lode::Logger::Info("       lode ci update [package-root]");
        return 1;
    }

    const std::string firstArgument = PathToUtf8(fs::path(argv[1]));
    if (firstArgument == "add")
    {
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
                return 1;
            }
        }

        if (specification.empty())
        {
            Lode::Logger::Error("Usage: lode add [--dev] owner/repository[@version] [package-root]");
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
