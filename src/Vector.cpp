// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Vector.hpp"

namespace Lode
{

Vector::Vector() = default;

Vector::Vector(float x, float y, float z)
    : components{ x, y, z, 0.0f }, size(3)
{
}

Vector::Vector(float x, float y, float z, float w)
    : components{ x, y, z, w }, size(4)
{
}

size_t Vector::Size() const
{
    return size;
}

float Vector::X() const
{
    return components[0];
}

float Vector::Y() const
{
    return components[1];
}

float Vector::Z() const
{
    return components[2];
}

float Vector::W() const
{
    return size > 3 ? components[3] : 0.0f;
}

} // namespace Lode
