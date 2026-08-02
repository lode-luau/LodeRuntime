#pragma once

#include "Lode/Error.hpp"
#include <variant>
#include <utility>
#include <stdexcept>

namespace Lode
{

template <typename T>
class Result
{
public:
    Result(const T& value) : data_(value) {}
    Result(T&& value) : data_(std::move(value)) {}
    Result(const Error& error) : data_(error) {}
    Result(Error&& error) : data_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool IsError() const { return std::holds_alternative<Error>(data_); }

    [[nodiscard]] const T& GetValue() const
    {
        if (IsError())
            throw std::runtime_error("Attempted to access value on error Result: " + GetError().GetMessage());
        return std::get<T>(data_);
    }

    [[nodiscard]] T& GetValue()
    {
        if (IsError())
            throw std::runtime_error("Attempted to access value on error Result: " + GetError().GetMessage());
        return std::get<T>(data_);
    }

    [[nodiscard]] const Error& GetError() const
    {
        if (IsOk())
            throw std::runtime_error("Attempted to access error on ok Result");
        return std::get<Error>(data_);
    }

    explicit operator bool() const { return IsOk(); }

private:
    std::variant<T, Error> data_;
};

template <>
class Result<void>
{
public:
    Result() : error_(Error()) {}
    Result(const Error& error) : error_(error) {}
    Result(Error&& error) : error_(std::move(error)) {}

    [[nodiscard]] bool IsOk() const { return !error_.HasError(); }
    [[nodiscard]] bool IsError() const { return error_.HasError(); }

    [[nodiscard]] const Error& GetError() const { return error_; }

    explicit operator bool() const { return IsOk(); }

private:
    Error error_;
};

} // namespace Lode
