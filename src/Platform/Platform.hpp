#pragma once

#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include <string_view>
#include <memory>

namespace Lode::Platform
{

class DynamicLibrary
{
public:
    virtual ~DynamicLibrary() = default;

    virtual Result<void*> GetSymbol(std::string_view name) const = 0;
    virtual void Close() = 0;

    static Result<std::shared_ptr<DynamicLibrary>> Open(std::string_view path);
};

} // namespace Lode::Platform
