// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Platform/Platform.hpp"
#include "lua.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Lode
{

typedef int (*LodeModuleInitFn)(lua_State* L);

class NativeModuleRegistry
{
public:
    NativeModuleRegistry() = default;
    ~NativeModuleRegistry() = default;

    NativeModuleRegistry(const NativeModuleRegistry&) = delete;
    NativeModuleRegistry& operator=(const NativeModuleRegistry&) = delete;

    static NativeModuleRegistry& GetGlobalRegistry();

    bool IsLoaded(const std::string& moduleName) const;
    void RegisterModule(const std::string& moduleName, std::shared_ptr<Platform::DynamicLibrary> library);
    std::shared_ptr<Platform::DynamicLibrary> GetModule(const std::string& moduleName) const;

    // --- Static Module Registration (for statically linked deployments; target validation is separate) ---
    void RegisterStaticModule(const std::string& moduleName, LodeModuleInitFn initFn);
    [[nodiscard]] bool HasStaticModule(const std::string& moduleName) const;
    [[nodiscard]] LodeModuleInitFn GetStaticModule(const std::string& moduleName) const;

    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Platform::DynamicLibrary>> loadedModules_;
    std::unordered_map<std::string, LodeModuleInitFn> staticModules_;
};

} // namespace Lode
