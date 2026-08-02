#include "Registry.hpp"

namespace Lode
{

bool NativeModuleRegistry::IsLoaded(const std::string& moduleName) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return loadedModules_.find(moduleName) != loadedModules_.end();
}

void NativeModuleRegistry::RegisterModule(const std::string& moduleName, std::shared_ptr<Platform::DynamicLibrary> library)
{
    std::lock_guard<std::mutex> lock(mutex_);
    loadedModules_[moduleName] = library;
}

std::shared_ptr<Platform::DynamicLibrary> NativeModuleRegistry::GetModule(const std::string& moduleName) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loadedModules_.find(moduleName);
    if (it != loadedModules_.end())
    {
        return it->second;
    }
    return nullptr;
}

void NativeModuleRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    loadedModules_.clear();
}

} // namespace Lode
