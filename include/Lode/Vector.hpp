// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include <array>
#include <cstddef>

namespace Lode
{

/** @brief Stores a Luau vector with its configured component count. */
struct LODE_API Vector
{
    Vector();
    Vector(float x, float y, float z);
    Vector(float x, float y, float z, float w);

    /** @brief Returns the number of active components. */
    [[nodiscard]] size_t Size() const;
    /** @brief Returns the first component. */
    [[nodiscard]] float X() const;
    /** @brief Returns the second component. */
    [[nodiscard]] float Y() const;
    /** @brief Returns the third component. */
    [[nodiscard]] float Z() const;
    /** @brief Returns the fourth component, or zero for a 3-component vector. */
    [[nodiscard]] float W() const;

    // Public storage preserves simple aggregate-style construction and access.
    std::array<float, 4> components{};
    size_t size = 3;
};

} // namespace Lode
