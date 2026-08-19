// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Lode::Detail
{

std::array<std::uint8_t, 32> Sha256(std::string_view data);
std::string ToHex(const std::array<std::uint8_t, 32>& bytes);
std::string Sha256Hex(std::string_view data);

} // namespace Lode::Detail
