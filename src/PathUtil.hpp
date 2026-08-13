// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Lode::Detail
{
// Converts a UTF-8 path string to a std::filesystem::path.
//
// std::filesystem::u8path was deprecated in C++20 (and is slated for removal
// in C++26); constructing the path directly from a char8_t view is the
// portable, non-deprecated equivalent on every supported platform.
inline std::filesystem::path PathFromUtf8(std::string_view path)
{
    std::u8string_view u8(reinterpret_cast<const char8_t*>(path.data()), path.size());
    return std::filesystem::path(u8);
}

// Converts a std::filesystem::path back to a UTF-8 string.
inline std::string PathToUtf8(const std::filesystem::path& path)
{
    std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}
} // namespace Lode::Detail
