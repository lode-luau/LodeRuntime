// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lodeffi
{

// Storage class of a parameter as seen by the platform ABI. The classes
// preserve the declared C width for libffi while the native call frame owns
// suitably aligned backing storage for every argument.
enum class ArgClass : uint8_t
{
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    Ptr, // pointer-sized value, NULL-able
    F32, // float
    F64, // double
    Struct, // named typedef struct/union passed by value
};

// How the return value is materialized back into Luau.
enum class RetKind : uint8_t
{
    Void,
    Bool, // C bool/_Bool, materialized as Luau number 0 or 1
    I8,   // signed 8-bit integer
    U8,   // unsigned 8-bit integer
    I16,  // signed 16-bit integer
    U16,  // unsigned 16-bit integer
    I32,  // signed 32-bit integer
    U32,  // unsigned 32-bit integer
    I64,  // signed 64-bit integer
    U64,  // unsigned 64-bit integer
    Ptr,  // pointer-sized value: pushed as lightuserdata
    F32,  // float: pushed as a Lua number
    F64,  // double: pushed as a Lua number
    Struct, // named typedef struct/union: returned as a Luau buffer
};

enum class CallingConvention : uint8_t
{
    Default,
    Cdecl,
    Stdcall,
};

struct Prototype
{
    std::string name;
    std::vector<ArgClass> args;
    // Empty for scalar/pointer arguments. A Struct entry names the matching
    // layout in CdefParseResult::structs; that lookup happens once at bind.
    std::vector<std::string> argStructNames;
    RetKind ret = RetKind::Void;
    std::string retStructName;
    CallingConvention convention = CallingConvention::Default;

    // Human-readable reconstruction used in error messages, e.g.
    // "int MessageBoxW(ptr, cstring, cstring, unsigned)".
    [[nodiscard]] std::string Describe() const;
};

// Parsed aggregate layout retained independently from a function prototype.
// Field classes are enough to construct the libffi FFI_TYPE_STRUCT elements
// during bind; size and alignment are finalized by libffi for the active ABI.
struct StructLayout
{
    std::string name;
    std::vector<ArgClass> fields;
    // Mirrors fields: identifies the nested layout for an ArgClass::Struct.
    std::vector<std::string> fieldStructNames;
    std::vector<std::string> fieldNames;
    std::vector<size_t> fieldCounts;
    bool isUnion = false;

    // Stable identity used by bound call plans. A struct layout is resolved
    // during cdef parsing and never rediscovered while invoking a function.
    [[nodiscard]] bool IsDefined() const { return !fields.empty(); }
};

// Maximum supported parameter count. Beyond this the bind fails at
// prototype-resolution time with a clear error instead of silently spilling
// arguments incorrectly.
inline constexpr size_t kMaxArity = 16;

// Scalar layout is fixed by the ABI class, not by the spelling used in the
// cdef. Struct layouts will extend this same descriptor path in v2.
[[nodiscard]] constexpr size_t SizeOf(ArgClass type)
{
    switch (type)
    {
        case ArgClass::Bool:
        case ArgClass::I8:
        case ArgClass::U8: return 1;
        case ArgClass::I16:
        case ArgClass::U16: return 2;
        case ArgClass::I32:
        case ArgClass::U32:
        case ArgClass::F32: return 4;
        case ArgClass::I64:
        case ArgClass::U64:
        case ArgClass::F64: return 8;
        case ArgClass::Ptr: return sizeof(void*);
    }
    return 0;
}

[[nodiscard]] constexpr size_t AlignOf(ArgClass type)
{
    switch (type)
    {
        case ArgClass::Bool:
        case ArgClass::I8:
        case ArgClass::U8: return 1;
        case ArgClass::I16:
        case ArgClass::U16: return 2;
        case ArgClass::I32:
        case ArgClass::U32:
        case ArgClass::F32: return 4;
        case ArgClass::I64:
        case ArgClass::U64:
        case ArgClass::F64: return 8;
        case ArgClass::Ptr: return alignof(void*);
    }
    return 1;
}

} // namespace lodeffi
