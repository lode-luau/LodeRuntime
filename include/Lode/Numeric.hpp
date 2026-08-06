// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Error.hpp"
#include "Lode/Result.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace Lode::Numeric
{

inline Result<size_t> ToSize(double value, const char* name)
{
    if (!std::isfinite(value) || value < 0.0 || std::trunc(value) != value ||
        value > static_cast<double>(std::numeric_limits<size_t>::max()))
    {
        return Error::Type(std::string(name) + " must be a finite non-negative integer");
    }

    return static_cast<size_t>(value);
}

inline Result<uint64_t> ToMilliseconds(double value, double multiplier, const char* name)
{
    if (!std::isfinite(value) || value < 0.0)
    {
        return Error::Type(std::string(name) + " must be a finite non-negative number");
    }

    double milliseconds = value * multiplier;
    if (!std::isfinite(milliseconds) ||
        milliseconds > static_cast<double>(std::numeric_limits<uint64_t>::max()))
    {
        return Error::Type(std::string(name) + " is out of range");
    }

    return static_cast<uint64_t>(milliseconds);
}

} // namespace Lode::Numeric
