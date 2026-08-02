#pragma once

#include "Platform/Platform.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Lode
{

class NativeModuleRegistry
{
public:
    NativeModuleRegistry() = default;
    ~NativeModuleRegistry() = default;

    NativeModuleRegistry(const NativeModuleRegistry&) = delete;
    NativeModuleRegistry& operator=(const NativeModuleRegistry&) = delete;

    bool IsLoaded(const std::string& moduleName) const;
    void RegisterModule(const std::string& moduleName, std::shared_ptr<Platform::DynamicLibrary> library);
    std::shared_ptr<Platform::DynamicLibrary> GetModule(const std::string& moduleName) const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Platform::DynamicLibrary>> loadedModules_;
};

} // namespace Lode
