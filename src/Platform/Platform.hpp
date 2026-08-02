#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include <string_view>
#include <memory>

namespace Lode::Platform
{

enum class OS
{
    Windows,
    Linux,
    MacOS,
    Android,
    IOS,
    BSD,
    Solaris,
    Haiku,
    WASM,
    Unknown
};

enum class Architecture
{
    x64,
    arm64,
    x86,
    wasm32,
    wasm64,
    Unknown
};

LODE_API OS GetOS();
LODE_API Architecture GetArchitecture();
LODE_API std::string_view GetOSName();
LODE_API std::string_view GetArchitectureName();

class LODE_API DynamicLibrary
{
public:
    virtual ~DynamicLibrary() = default;

    virtual Result<void*> GetSymbol(std::string_view name) const = 0;
    virtual void Close() = 0;

    static Result<std::shared_ptr<DynamicLibrary>> Open(std::string_view path);
};

} // namespace Lode::Platform
