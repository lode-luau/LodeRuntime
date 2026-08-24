// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "DynamicLibrary.hpp"

#include <filesystem>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace lodeffi
{

#if defined(_WIN32)

namespace
{
using AddDllDirectoryFn = DLL_DIRECTORY_COOKIE(WINAPI*)(PCWSTR);
using RemoveDllDirectoryFn = BOOL(WINAPI*)(DLL_DIRECTORY_COOKIE);

std::string FormatWin32Error(unsigned long code)
{
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (size == 0 || buffer == nullptr)
        return "error " + std::to_string(code);
    std::string message(size <= 2 ? 0 : size - 2, '\0'); // trim trailing \r\n
    int written = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), message.data(),
                                      static_cast<int>(message.size()), nullptr, nullptr);
    LocalFree(buffer);
    if (written <= 0) return "error " + std::to_string(code);
    return message;
}
} // namespace

DynamicLibrary::~DynamicLibrary() { Unload(); }

std::shared_ptr<DynamicLibrary> DynamicLibrary::Open(
    const std::string& name, const DynamicLibraryOptions& options, std::string* error)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), nullptr, 0);
    if (wlen <= 0)
    {
        if (error) *error = "invalid library name encoding";
        return nullptr;
    }
    std::wstring wname(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), static_cast<int>(name.size()), wname.data(), wlen);

    DWORD flags = 0;
    DLL_DIRECTORY_COOKIE directoryCookie = nullptr;
    RemoveDllDirectoryFn removeDllDirectory = nullptr;
    if (options.searchDllDirectory)
    {
        if (!std::filesystem::path(name).is_absolute())
        {
            if (error) *error = "searchDllDirectory requires an absolute library path";
            return nullptr;
        }
        // Keep the extra directory scoped to this load. Resolve the APIs at
        // runtime so the module still loads on older Windows versions.
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const auto addDllDirectory = kernel32 == nullptr ? nullptr
            : reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(kernel32, "AddDllDirectory"));
        removeDllDirectory = kernel32 == nullptr ? nullptr
            : reinterpret_cast<RemoveDllDirectoryFn>(GetProcAddress(kernel32, "RemoveDllDirectory"));
        const std::wstring directory = std::filesystem::path(wname).parent_path().wstring();
        if (addDllDirectory != nullptr && removeDllDirectory != nullptr && !directory.empty())
            directoryCookie = addDllDirectory(directory.c_str());

        // DLL_LOAD_DIR is the direct path's parent; USER_DIRS makes the
        // scoped AddDllDirectory fallback available to dependent imports.
        flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                (directoryCookie != nullptr ? LOAD_LIBRARY_SEARCH_USER_DIRS : 0);
    }

    HMODULE handle = LoadLibraryExW(wname.c_str(), nullptr, flags);
    if (handle == nullptr && options.searchDllDirectory && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        // LOAD_LIBRARY_SEARCH_* is unavailable on older Windows releases.
        // Keep the legacy per-load behavior as a compatibility fallback.
        handle = LoadLibraryExW(wname.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (directoryCookie != nullptr)
        removeDllDirectory(directoryCookie);
    if (handle == nullptr)
    {
        if (error) *error = FormatWin32Error(GetLastError());
        return nullptr;
    }

    auto lib = std::shared_ptr<DynamicLibrary>(new DynamicLibrary());
    lib->handle_ = handle;
    return lib;
}

std::shared_ptr<DynamicLibrary> DynamicLibrary::OpenSelf(std::string* error)
{
    HMODULE handle = GetModuleHandleW(nullptr);
    if (handle == nullptr)
    {
        if (error) *error = FormatWin32Error(GetLastError());
        return nullptr;
    }

    auto lib = std::shared_ptr<DynamicLibrary>(new DynamicLibrary());
    lib->handle_ = handle;
    // GetModuleHandleW does not increment the module reference count.
    lib->ownsHandle_ = false;
    return lib;
}

void* DynamicLibrary::Symbol(const std::string& name, std::string* error) const
{
    FARPROC sym = GetProcAddress(static_cast<HMODULE>(handle_), name.c_str());
    if (sym == nullptr)
    {
        if (error)
            *error = "symbol '" + name + "' not found: " + FormatWin32Error(GetLastError());
        return nullptr;
    }
    return reinterpret_cast<void*>(sym);
}

void DynamicLibrary::Close()
{
    // Bound closures capture this object and may still call their resolved
    // symbols. The handle is released by the destructor after those closures
    // are gone.
}

void DynamicLibrary::Unload()
{
    if (handle_ != nullptr && ownsHandle_)
    {
        FreeLibrary(static_cast<HMODULE>(handle_));
        handle_ = nullptr;
    }
}

#else // POSIX

DynamicLibrary::~DynamicLibrary() { Unload(); }

std::shared_ptr<DynamicLibrary> DynamicLibrary::Open(
    const std::string& name, const DynamicLibraryOptions& options, std::string* error)
{
    if (options.searchDllDirectory)
    {
        if (error) *error = "searchDllDirectory is only supported on Windows";
        return nullptr;
    }
    void* handle = dlopen(name.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr)
    {
        if (error)
        {
            const char* msg = dlerror();
            *error = msg != nullptr ? msg : ("cannot open '" + name + "'");
        }
        return nullptr;
    }

    auto lib = std::shared_ptr<DynamicLibrary>(new DynamicLibrary());
    lib->handle_ = handle;
    return lib;
}

std::shared_ptr<DynamicLibrary> DynamicLibrary::OpenSelf(std::string* error)
{
    void* handle = dlopen(nullptr, RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr)
    {
        if (error)
        {
            const char* msg = dlerror();
            *error = msg != nullptr ? msg : "cannot open the current process";
        }
        return nullptr;
    }

    auto lib = std::shared_ptr<DynamicLibrary>(new DynamicLibrary());
    lib->handle_ = handle;
    return lib;
}

void* DynamicLibrary::Symbol(const std::string& name, std::string* error) const
{
    dlerror();
    void* sym = dlsym(handle_, name.c_str());
    if (sym == nullptr)
    {
        if (error)
        {
            const char* msg = dlerror();
            *error = msg != nullptr ? msg : ("symbol '" + name + "' not found");
        }
        return nullptr;
    }
    return sym;
}

void DynamicLibrary::Close()
{
    // See the Windows implementation: bound closures retain the handle.
}

void DynamicLibrary::Unload()
{
    if (handle_ != nullptr && ownsHandle_)
    {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

#endif

} // namespace lodeffi
