// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Error.hpp"
#include <variant>
#include <utility>
#include <stdexcept>

namespace Lode
{

/**
 * @brief Represents a value that may either be a successful result or an Error.
 * 
 * Lode::Result<T> is the standard error-handling mechanism in the Lode C++ API,
 * allowing safe unwrapping of Luau errors without relying heavily on C++ exceptions.
 */
template <typename T>
class Result
{
public:
    /** @brief Constructs a successful Result from a value. */
    Result(const T& value) : data_(value) {}
    /** @brief Constructs a successful Result from a value (move). */
    Result(T&& value) : data_(std::move(value)) {}
    /** @brief Constructs a failed Result from an Error. */
    Result(const Error& error) : data_(error) {}
    /** @brief Constructs a failed Result from an Error (move). */
    Result(Error&& error) : data_(std::move(error)) {}

    /** @brief Checks if the Result contains a value. */
    [[nodiscard]] bool IsOk() const { return std::holds_alternative<T>(data_); }
    /** @brief Checks if the Result contains an Error. */
    [[nodiscard]] bool IsError() const { return std::holds_alternative<Error>(data_); }

    /** 
     * @brief Gets the value safely. 
     * @throws std::runtime_error if the Result contains an error.
     */
    [[nodiscard]] const T& GetValue() const
    {
        if (IsError())
            throw std::runtime_error("Attempted to access value on error Result: " + GetError().GetMessage());
        return std::get<T>(data_);
    }

    /** 
     * @brief Gets the value safely (mutable). 
     * @throws std::runtime_error if the Result contains an error.
     */
    [[nodiscard]] T& GetValue()
    {
        if (IsError())
            throw std::runtime_error("Attempted to access value on error Result: " + GetError().GetMessage());
        return std::get<T>(data_);
    }

    /** 
     * @brief Gets the Error safely. 
     * @throws std::runtime_error if the Result contains a value.
     */
    [[nodiscard]] const Error& GetError() const
    {
        if (IsOk())
            throw std::runtime_error("Attempted to access error on ok Result");
        return std::get<Error>(data_);
    }

    /** @brief Implicit conversion to boolean (true if IsOk). */
    explicit operator bool() const { return IsOk(); }

private:
    std::variant<T, Error> data_;
};

/**
 * @brief Specialization of Result for void returns.
 */
template <>
class Result<void>
{
public:
    /** @brief Constructs a successful void Result. */
    Result() : error_(Error()) {}
    /** @brief Constructs a failed void Result from an Error. */
    Result(const Error& error) : error_(error) {}
    /** @brief Constructs a failed void Result from an Error (move). */
    Result(Error&& error) : error_(std::move(error)) {}

    /** @brief Checks if the Result was successful. */
    [[nodiscard]] bool IsOk() const { return !error_.HasError(); }
    /** @brief Checks if the Result contains an Error. */
    [[nodiscard]] bool IsError() const { return error_.HasError(); }

    /** 
     * @brief Gets the Error safely. 
     */
    [[nodiscard]] const Error& GetError() const { return error_; }

    explicit operator bool() const { return IsOk(); }

private:
    Error error_;
};

} // namespace Lode
