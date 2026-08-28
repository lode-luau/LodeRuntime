// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "FfiTypes.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace lodeffi
{

// Parses a subset of C declarations (LuaJIT ffi.cdef style) into resolved
// prototypes. Supported: scalar/primitive types with signed/unsigned/const
// qualifiers, intN_t/uintN_t family, size_t/intptr_t family, simple typedef
// aliases, C enums (as int32), opaque struct/union pointers, single-level
// pointers, and fixed array parameters (which C adjusts to pointers). Any pointer accepts
// strings/buffers where applicable at call time. An opaque aggregate typedef
// (for example `typedef struct Handle Handle`) must likewise be used with
// `Handle*`. A defined `typedef struct { ... } Name` may be passed by value
// as a Luau buffer or returned as one; function pointers are unsupported.
// On 64-bit Windows-compatible ABIs, common convention markers such as
// `WINAPI` and `__stdcall` are accepted as aliases of the default ABI.
//
// Throws std::runtime_error with a 1-based line reference on any parse error.
struct CdefParseResult
{
    std::vector<Prototype> prototypes;
    std::vector<StructLayout> structs;
    // Resolved typedefs declared in this block. The runtime stores these per
    // State so a later ffi.cdef/ffi.new/ffi.sizeof call can use the name.
    std::unordered_map<std::string, ArgClass> aliases;
};

[[nodiscard]] CdefParseResult ParseCdef(const std::string& source);

} // namespace lodeffi
