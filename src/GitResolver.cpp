// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "GitResolver.hpp"

#include "PathUtil.hpp"
#include "Sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Lode::Package
{

namespace
{

namespace fs = std::filesystem;
using Lode::Detail::PathFromUtf8;
using Lode::Detail::PathToUtf8;

struct ProcessResult
{
    int exitCode = -1;
    std::string output;
    std::string error;
};

void AddError(GitCheckoutResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    return value.substr(first);
}

bool IsCommitHash(const std::string& value)
{
    if (value.size() != 40 && value.size() != 64)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

std::string NormalizeRepositoryReference(const std::string& repository)
{
    constexpr std::string_view prefix = "github:";
    if (repository.rfind(prefix, 0) != 0)
        return repository;

    const std::string slug = repository.substr(prefix.size());
    if (slug.empty() || slug.find('/') == std::string::npos)
        return repository;
    return "https://github.com/" + slug;
}

#if defined(_WIN32)

std::wstring ToWide(const std::string& value)
{
    return PathFromUtf8(value).wstring();
}

std::wstring QuoteWindowsArgument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'\"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

ProcessResult RunGit(const std::vector<std::string>& arguments)
{
    ProcessResult result;
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0))
    {
        result.error = "CreatePipe failed with Windows error " + std::to_string(GetLastError());
        return result;
    }
    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine = QuoteWindowsArgument(L"git");
    for (const std::string& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteWindowsArgument(ToWide(argument));
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writeHandle;
    startupInfo.hStdError = writeHandle;
    PROCESS_INFORMATION processInfo{};

    if (!CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
    {
        result.error = "CreateProcess failed with Windows error " + std::to_string(GetLastError());
        CloseHandle(readHandle);
        CloseHandle(writeHandle);
        return result;
    }

    CloseHandle(writeHandle);
    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readHandle, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &bytesRead, nullptr) && bytesRead > 0)
        result.output.append(buffer.data(), bytesRead);

    CloseHandle(readHandle);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return result;
}

#else

ProcessResult RunGit(const std::vector<std::string>& arguments)
{
    ProcessResult result;
    int pipeDescriptors[2]{};
    if (pipe(pipeDescriptors) != 0)
    {
        result.error = "pipe failed";
        return result;
    }

    const pid_t child = fork();
    if (child < 0)
    {
        close(pipeDescriptors[0]);
        close(pipeDescriptors[1]);
        result.error = "fork failed";
        return result;
    }
    if (child == 0)
    {
        dup2(pipeDescriptors[1], STDOUT_FILENO);
        dup2(pipeDescriptors[1], STDERR_FILENO);
        close(pipeDescriptors[0]);
        close(pipeDescriptors[1]);

        std::vector<char*> argv;
        std::vector<std::string> values;
        values.reserve(arguments.size() + 1);
        values.push_back("git");
        values.insert(values.end(), arguments.begin(), arguments.end());
        for (std::string& value : values)
            argv.push_back(value.data());
        argv.push_back(nullptr);
        execvp("git", argv.data());
        _exit(127);
    }

    close(pipeDescriptors[1]);
    int status = 0;
    waitpid(child, &status, 0);
    std::array<char, 4096> buffer{};
    ssize_t bytesRead = 0;
    while ((bytesRead = read(pipeDescriptors[0], buffer.data(), buffer.size())) > 0)
        result.output.append(buffer.data(), static_cast<size_t>(bytesRead));
    close(pipeDescriptors[0]);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

#endif

} // namespace

GitCheckoutResult CheckoutGitPackageInternal(const std::string& repository,
                                             const std::string& requestedCommit,
                                             const std::filesystem::path& stagingDirectory)
{
    GitCheckoutResult result;
    if (repository.empty())
    {
        AddError(result, "Cannot resolve Git dependency: repository reference is empty.");
        return result;
    }

    std::error_code ec;
    if (!fs::create_directories(stagingDirectory, ec) && ec)
    {
        AddError(result, "Cannot create Git staging directory '" +
            PathToUtf8(stagingDirectory) + "': " + ec.message());
        return result;
    }

    const fs::path packageRoot = stagingDirectory /
        Lode::Detail::Sha256Hex(repository + "\n" + requestedCommit);
    if (fs::exists(packageRoot, ec))
    {
        AddError(result, "Git staging destination already exists: " + PathToUtf8(packageRoot));
        return result;
    }

    const std::string cloneRepository = NormalizeRepositoryReference(repository);
    std::vector<std::string> cloneArguments = {
        "clone", "--depth", "1", "--quiet"
    };
    if (!requestedCommit.empty())
        cloneArguments.push_back("--no-checkout");
    cloneArguments.insert(cloneArguments.end(), {
        "--", cloneRepository, PathToUtf8(packageRoot)
    });
    const ProcessResult clone = RunGit(cloneArguments);
    if (clone.exitCode != 0)
    {
        AddError(result, "Git clone failed for '" + repository + "': " +
            Trim(clone.output.empty() ? clone.error : clone.output));
        return result;
    }

    if (!requestedCommit.empty())
    {
        const ProcessResult fetch = RunGit({
            "-C", PathToUtf8(packageRoot), "fetch", "--depth", "1",
            "--quiet", "origin", requestedCommit
        });
        if (fetch.exitCode != 0)
        {
            AddError(result, "Git commit '" + requestedCommit + "' could not be fetched from '" +
                repository + "': " + Trim(fetch.output));
            fs::remove_all(packageRoot, ec);
            return result;
        }

        const ProcessResult checkout = RunGit({
            "-C", PathToUtf8(packageRoot), "checkout", "--quiet",
            "--detach", requestedCommit
        });
        if (checkout.exitCode != 0)
        {
            AddError(result, "Git commit '" + requestedCommit + "' could not be checked out from '" +
                repository + "': " + Trim(checkout.output));
            fs::remove_all(packageRoot, ec);
            return result;
        }
    }

    const ProcessResult revision = RunGit({
        "-C", PathToUtf8(packageRoot), "rev-parse", "--verify", "HEAD"
    });
    const std::string commit = Trim(revision.output);
    if (revision.exitCode != 0 || !IsCommitHash(commit))
    {
        AddError(result, "Git repository '" + repository +
            "' did not produce a valid HEAD commit.");
        fs::remove_all(packageRoot, ec);
        return result;
    }

    fs::remove_all(packageRoot / ".git", ec);
    if (ec)
    {
        AddError(result, "Cannot remove Git metadata from '" +
            PathToUtf8(packageRoot) + "': " + ec.message());
        fs::remove_all(packageRoot, ec);
        return result;
    }

    result.packageRoot = packageRoot;
    if (!requestedCommit.empty() &&
        (requestedCommit.size() != commit.size() ||
         !std::equal(requestedCommit.begin(), requestedCommit.end(), commit.begin(),
            [](unsigned char left, unsigned char right) {
                return std::tolower(left) == std::tolower(right);
            })))
    {
        AddError(result, "Git checkout resolved a different commit than the lockfile requested.");
        fs::remove_all(packageRoot, ec);
        return result;
    }

    result.commit = commit;
    return result;
}

GitCheckoutResult CheckoutGitPackage(const std::string& repository,
                                     const std::filesystem::path& stagingDirectory)
{
    return CheckoutGitPackageInternal(repository, {}, stagingDirectory);
}

GitCheckoutResult CheckoutGitPackageAtCommit(const std::string& repository,
                                             const std::string& commit,
                                             const std::filesystem::path& stagingDirectory)
{
    if (commit.empty())
    {
        GitCheckoutResult result;
        result.errors.push_back("Locked Git installation requires a resolved commit.");
        return result;
    }
    return CheckoutGitPackageInternal(repository, commit, stagingDirectory);
}

} // namespace Lode::Package
