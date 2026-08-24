// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>

namespace lodeffi
{

struct DynamicLibraryOptions
{
    // Windows only: resolve dependencies beside an absolute library path.
    bool searchDllDirectory = false;
};

// RAII handle over LoadLibraryW/FreeLibrary (Windows) or dlopen/dlclose
// (POSIX). Owned by a shared_ptr captured by every bound closure, so a
// library stays alive exactly as long as any of its bound functions can be
// called; Close() only releases it early.
class DynamicLibrary
{
public:
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    // Opens a library by name. On Windows the name is converted from UTF-8
    // and the system loader appends ".dll" when no extension is present;
    // on POSIX dlopen semantics apply verbatim. On failure returns nullptr
    // and fills *error with an OS-formatted message.
    [[nodiscard]] static std::shared_ptr<DynamicLibrary> Open(
        const std::string& name, const DynamicLibraryOptions& options, std::string* error);

    // Opens the image that contains the current process. This exposes symbols
    // already loaded by the executable and its global dependencies (ffi.C).
    [[nodiscard]] static std::shared_ptr<DynamicLibrary> OpenSelf(std::string* error);

    // Resolves a symbol. On failure returns nullptr and fills *error.
    [[nodiscard]] void* Symbol(const std::string& name, std::string* error) const;

    // Marks the public handle as closed. Bound closures keep the underlying
    // OS handle alive until this object is destroyed, so they remain callable.
    void Close();

private:
    DynamicLibrary() = default;

    void Unload();

#if defined(_WIN32)
    void* handle_ = nullptr; // HMODULE
#else
    void* handle_ = nullptr; // void* from dlopen
#endif
    bool ownsHandle_ = true;
};

} // namespace lodeffi
