// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include <string>
#include <string_view>

namespace Lode
{

/**
 * @brief Categorizes the type of error that occurred within Lode Runtime.
 */
enum class ErrorType
{
    None = 0,
    RuntimeError,
    ModuleError,
    TypeError,
    PlatformError,
    SyntaxError
};

/**
 * @brief Represents an error condition with an associated type and message.
 */
class LODE_API Error
{
public:
    /** @brief Constructs an empty (None) error. */
    Error() : type_(ErrorType::None), message_("") {}
    /** @brief Constructs a specific error. */
    Error(ErrorType type, std::string_view message) : type_(type), message_(message) {}

    /** @brief Gets the category of the error. */
    [[nodiscard]] ErrorType GetType() const { return type_; }
    /** @brief Gets the detailed error message. */
    [[nodiscard]] const std::string& GetMessage() const { return message_; }
    /** @brief Checks if this is an actual error (not None). */
    [[nodiscard]] bool HasError() const { return type_ != ErrorType::None; }

    /** @brief Creates a generic runtime error. */
    static Error Runtime(std::string_view message) { return Error(ErrorType::RuntimeError, message); }
    /** @brief Creates a module resolution/loading error. */
    static Error Module(std::string_view message) { return Error(ErrorType::ModuleError, message); }
    /** @brief Creates a type mismatch or casting error. */
    static Error Type(std::string_view message) { return Error(ErrorType::TypeError, message); }
    /** @brief Creates a platform or C++ boundary error. */
    static Error Platform(std::string_view message) { return Error(ErrorType::PlatformError, message); }
    /** @brief Creates a Luau syntax/compilation error. */
    static Error Syntax(std::string_view message) { return Error(ErrorType::SyntaxError, message); }

private:
    ErrorType type_;
    std::string message_;
};

} // namespace Lode
