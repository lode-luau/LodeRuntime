#pragma once

#include "Lode/Export.hpp"
#include <string>
#include <string_view>

namespace Lode
{

enum class ErrorType
{
    None = 0,
    RuntimeError,
    ModuleError,
    TypeError,
    PlatformError,
    SyntaxError
};

class LODE_API Error
{
public:
    Error() : type_(ErrorType::None), message_("") {}
    Error(ErrorType type, std::string_view message) : type_(type), message_(message) {}

    [[nodiscard]] ErrorType GetType() const { return type_; }
    [[nodiscard]] const std::string& GetMessage() const { return message_; }
    [[nodiscard]] bool HasError() const { return type_ != ErrorType::None; }

    static Error Runtime(std::string_view message) { return Error(ErrorType::RuntimeError, message); }
    static Error Module(std::string_view message) { return Error(ErrorType::ModuleError, message); }
    static Error Type(std::string_view message) { return Error(ErrorType::TypeError, message); }
    static Error Platform(std::string_view message) { return Error(ErrorType::PlatformError, message); }

private:
    ErrorType type_;
    std::string message_;
};

} // namespace Lode
