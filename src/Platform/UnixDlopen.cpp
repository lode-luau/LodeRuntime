// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#if !defined(_WIN32)

#include "Platform.hpp"
#include <dlfcn.h>

namespace Lode::Platform
{

class UnixDynamicLibrary : public DynamicLibrary
{
public:
    explicit UnixDynamicLibrary(void* handle) : handle_(handle) {}

    ~UnixDynamicLibrary() override
    {
        Close();
    }

    Result<void*> GetSymbol(std::string_view name) const override
    {
        if (!handle_)
            return Error::Platform("Dynamic library handle is invalid");

        dlerror(); // Clear old error
        std::string symName(name);
        void* sym = dlsym(handle_, symName.c_str());
        const char* err = dlerror();
        if (err)
        {
            return Error::Platform("Failed to find symbol: " + symName + " (" + err + ")");
        }
        return sym;
    }

    void Close() override
    {
        if (handle_)
        {
            dlclose(handle_);
            handle_ = nullptr;
        }
    }

private:
    void* handle_ = nullptr;
};

Result<std::shared_ptr<DynamicLibrary>> DynamicLibrary::Open(std::string_view path)
{
    std::string pathStr(path);
    void* handle = dlopen(pathStr.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char* err = dlerror();
        return Error::Platform("Failed to dlopen library: " + pathStr + " (" + (err ? err : "unknown error") + ")");
    }

    std::shared_ptr<DynamicLibrary> lib = std::make_shared<UnixDynamicLibrary>(handle);
    return lib;
}

} // namespace Lode::Platform

#endif
