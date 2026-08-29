// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

// ffi: call C functions exported by any dynamic library directly from Luau.
//
// Design highlights:
//   * Declarations are parsed once at load time (CdefParser) into resolved
//     prototypes; nothing is parsed per call.
//   * Symbols are resolved once at bind time; a missing symbol fails the
//     bind loudly instead of failing (or crashing) at first call.
//   * Each prototype prepares a libffi call interface (ffi_cif) exactly
//     once at bind time -- the "prepare the layout once, reuse it" pattern
//     recommended upstream. Per call, the cost is argument extraction plus
//     a single ffi_call through the cached plan.
//   * The library handle is owned by a shared_ptr captured by every bound
//     closure, so bound functions keep the library alive even after Close().
//
// Blocking calls run on the event-loop thread. callAsync dispatches a bound
// call through libuv's worker pool and resumes the yielding coroutine.

#include "Lode/Module.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/CFunctionCallContext.hpp"
#include "Lode/State.hpp"
#include "Lode/Task.hpp"
#include "Lode/Table.hpp"
#include "lua.h"
#include "lualib.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <uv.h>

#include <ffi.h>

#include "CdefParser.hpp"
#include "DynamicLibrary.hpp"
#include "FfiTypes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{

[[noreturn]] void BindError(const std::string& message)
{
    throw std::runtime_error(message);
}

using namespace lodeffi;

constexpr uint64_t kMaxSafeInteger = (uint64_t{1} << 53) - 1;

thread_local int gLastError = 0;

void CaptureLastError()
{
#if defined(_WIN32)
    gLastError = static_cast<int>(GetLastError());
#else
    gLastError = errno;
#endif
}

size_t ParseTypeSize(const std::string& declaration)
{
    // Reuse the actual cdef parser so sizeof and ffi.load cannot disagree on
    // aliases, qualifiers, or pointer adjustments. The dummy parameter name
    // is discarded after parsing.
    const auto parsed = ParseCdef("void __ffi_sizeof(" + declaration + " value);");
    if (parsed.prototypes.size() != 1 || parsed.prototypes.front().args.size() != 1)
        BindError("ffi.sizeof: expected one C type");
    return SizeOf(parsed.prototypes.front().args.front());
}

struct RawStructLayout
{
    size_t size = 0;
    size_t alignment = 1;
    std::unordered_map<std::string, size_t> offsets;
};

RawStructLayout ParseStructLayout(const std::string& fields)
{
    RawStructLayout layout;
    size_t offset = 0;
    size_t structAlignment = 1;
    size_t begin = 0;
    bool sawField = false;
    while (begin < fields.size())
    {
        const size_t end = fields.find(';', begin);
        std::string field = fields.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto first = field.find_first_not_of(" \t\r\n");
        if (first != std::string::npos)
        {
            const auto last = field.find_last_not_of(" \t\r\n");
            field = field.substr(first, last - first + 1);
            size_t count = 1;
            const size_t openBracket = field.find('[');
            if (openBracket != std::string::npos)
            {
                const size_t closeBracket = field.find(']', openBracket);
                if (closeBracket == std::string::npos)
                    BindError("ffi.struct: expected ']' after array size");
                const std::string extent = field.substr(openBracket + 1, closeBracket - openBracket - 1);
                if (extent.empty()) BindError("ffi.struct: array field requires a fixed size");
                count = static_cast<size_t>(std::stoull(extent));
                if (count == 0) BindError("ffi.struct: array field size must not be zero");
                field.erase(openBracket, closeBracket - openBracket + 1);
                field.erase(field.find_last_not_of(" \t\r\n") + 1);
            }
            const auto parsed = ParseCdef("void __ffi_struct_field(" + field + ");");
            if (parsed.prototypes.size() != 1 || parsed.prototypes.front().args.size() != 1)
                BindError("ffi.struct: each field must have one supported C type");
            std::string name;
            const size_t functionOpen = field.find("(*");
            if (functionOpen != std::string::npos)
            {
                const size_t nameBegin = functionOpen + 2;
                const size_t nameEnd = field.find(')', nameBegin);
                if (nameEnd == std::string::npos || nameEnd == nameBegin)
                    BindError("ffi.struct: malformed function-pointer field");
                name = field.substr(nameBegin, nameEnd - nameBegin);
            }
            else
            {
                const size_t nameBegin = field.find_last_of(" \t");
                if (nameBegin == std::string::npos || nameBegin + 1 >= field.size())
                    BindError("ffi.struct: each field requires a name");
                name = field.substr(nameBegin + 1);
            }
            const ArgClass type = parsed.prototypes.front().args.front();
            const size_t alignment = AlignOf(type);
            offset = (offset + alignment - 1) / alignment * alignment;
            if (!layout.offsets.emplace(name, offset).second)
                BindError("ffi.struct: duplicate field '" + name + "'");
            offset += SizeOf(type) * count;
            structAlignment = std::max(structAlignment, alignment);
            sawField = true;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (!sawField)
        BindError("ffi.struct: expected at least one field declaration");
    layout.size = (offset + structAlignment - 1) / structAlignment * structAlignment;
    layout.alignment = structAlignment;
    return layout;
}

RawStructLayout ParseUnionLayout(const std::string& fields)
{
    RawStructLayout layout;
    size_t begin = 0;
    bool sawField = false;
    while (begin < fields.size())
    {
        const size_t end = fields.find(';', begin);
        const std::string field = fields.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (field.find_first_not_of(" \t\r\n") != std::string::npos)
        {
            const RawStructLayout member = ParseStructLayout(field + ";");
            layout.size = std::max(layout.size, member.size);
            layout.alignment = std::max(layout.alignment, member.alignment);
            for (const auto& [name, _] : member.offsets)
            {
                if (!layout.offsets.emplace(name, 0).second)
                    BindError("ffi.union: duplicate field '" + name + "'");
            }
            sawField = true;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (!sawField) BindError("ffi.union: expected at least one field declaration");
    layout.size = (layout.size + layout.alignment - 1) / layout.alignment * layout.alignment;
    return layout;
}

bool ExtractPointerArg(const Lode::StackValue& v, void** out, std::string* err,
                       std::string* stringStorage);

bool ExtractStrictPointer(const Lode::StackValue& v, void** out, std::string* error)
{
    if (v.GetType() == Lode::ValueType::LightUserdata) { *out = v.AsLightUserdata(); return true; }
    if (v.GetType() == Lode::ValueType::Buffer) { const auto s=v.AsSpan(); *out=s.empty()?nullptr:s.data(); return true; }
    if (v.GetType() == Lode::ValueType::Table)
    {
        const auto table = v.AsTable();
        const auto closed = table.Get("__ffiClosed");
        if (closed.IsOk() && closed.GetValue().IsBoolean() && closed.GetValue().AsBoolean()) { *error="callback is closed"; return false; }
        const auto storage=table.Get("__ffiBuffer");
        if (storage.IsOk() && storage.GetValue().IsBuffer()) { const auto s=storage.GetValue().AsSpan(); *out=s.empty()?nullptr:s.data(); return true; }
        const auto pointer=table.Get("Pointer");
        if (pointer.IsOk() && pointer.GetValue().GetType()==Lode::ValueType::LightUserdata) { *out=pointer.GetValue().AsLightUserdata(); return true; }
    }
    *error="expected an opaque pointer, buffer, typed struct, or callback"; return false;
}

struct RegisteredStruct
{
    StructLayout layout;
    RawStructLayout raw;
};
struct TypeRegistry {
    std::unordered_map<std::string, RegisteredStruct> structs;
    std::unordered_map<std::string, Prototype> functionTypes;
    std::unordered_map<std::string, ArgClass> aliases;
};
std::mutex gTypeRegistryMutex;
std::unordered_map<lua_State*, TypeRegistry> gTypeRegistries;

RawStructLayout LayoutFor(const StructLayout& layout)
{
    RawStructLayout out;
    size_t offset = 0;
    size_t fieldIndex = 0;
    for (size_t i = 0; i < layout.fieldNames.size(); ++i)
    {
        const ArgClass type = layout.fields[fieldIndex];
        const size_t alignment = AlignOf(type);
        if (!layout.isUnion)
            offset = (offset + alignment - 1) / alignment * alignment;
        out.offsets.emplace(layout.fieldNames[i], layout.isUnion ? 0 : offset);
        if (layout.isUnion)
            offset = std::max(offset, SizeOf(type) * layout.fieldCounts[i]);
        else
            offset += SizeOf(type) * layout.fieldCounts[i];
        out.alignment = std::max(out.alignment, alignment);
        fieldIndex += layout.fieldCounts[i];
    }
    out.size = (offset + out.alignment - 1) / out.alignment * out.alignment;
    return out;
}

bool ExtractTypedBuffer(const Lode::StackValue& value, Lode::Value* out)
{
    if (!value.IsTable()) return false;
    const auto storage = value.AsTable().Get("__ffiBuffer");
    if (storage.IsError() || !storage.GetValue().IsBuffer()) return false;
    *out = storage.GetValue();
    return true;
}

std::optional<size_t> StrictOwnedSize(const Lode::StackValue& value)
{
    if (value.GetType() == Lode::ValueType::Buffer)
        return value.AsSpan().size();
    Lode::Value storage;
    if (ExtractTypedBuffer(value, &storage))
        return storage.AsSpan().size();
    return std::nullopt;
}

size_t StrictOffset(const Lode::StackValue& value, const char* api)
{
    if (!value.IsNumber()) BindError(std::string(api) + ": offset must be a number");
    const double offset = value.AsNumber();
    if (!std::isfinite(offset) || offset < 0 || std::floor(offset) != offset ||
        offset > static_cast<double>(std::numeric_limits<size_t>::max()))
        BindError(std::string(api) + ": offset must be a non-negative integer");
    return static_cast<size_t>(offset);
}

void StrictRange(const Lode::StackValue& owner, size_t offset, size_t bytes, const char* api)
{
    const auto size = StrictOwnedSize(owner);
    if (!size.has_value()) return; // C-owned memory has no discoverable bounds.
    if (offset > *size || bytes > *size - offset)
        BindError(std::string(api) + ": access exceeds Luau-owned storage");
}

void* StrictAddress(void* pointer, size_t offset, size_t alignment, const char* api)
{
    if (pointer == nullptr) BindError(std::string(api) + ": expected a non-NULL pointer");
    const uintptr_t base = reinterpret_cast<uintptr_t>(pointer);
    if (offset > std::numeric_limits<uintptr_t>::max() - base)
        BindError(std::string(api) + ": offset overflows the pointer address");
    const uintptr_t address = base + offset;
    if (alignment > 1 && address % alignment != 0)
        BindError(std::string(api) + ": address is not aligned for the requested type");
    return reinterpret_cast<void*>(address);
}

const char* CanonicalTypeName(ArgClass type)
{
    switch (type)
    {
        case ArgClass::Bool: return "bool";
        case ArgClass::I8: return "int8_t";
        case ArgClass::U8: return "uint8_t";
        case ArgClass::I16: return "int16_t";
        case ArgClass::U16: return "uint16_t";
        case ArgClass::I32: return "int32_t";
        case ArgClass::U32: return "uint32_t";
        case ArgClass::I64: return "int64_t";
        case ArgClass::U64: return "uint64_t";
        case ArgClass::Ptr: return "void*";
        case ArgClass::F32: return "float";
        case ArgClass::F64: return "double";
        case ArgClass::Struct: break;
    }
    return nullptr;
}

std::string RegistryPreamble(const TypeRegistry& registry)
{
    std::string declarations;
    for (const auto& [name, type] : registry.aliases)
    {
        const char* canonical = CanonicalTypeName(type);
        if (canonical != nullptr)
            declarations += "typedef " + std::string(canonical) + " " + name + ";\n";
    }
    for (const auto& [name, prototype] : registry.functionTypes)
    {
        auto retName = [](RetKind ret) -> const char* {
            switch (ret)
            {
                case RetKind::Void: return "void";
                case RetKind::Bool: return "bool";
                case RetKind::I8: return "int8_t";
                case RetKind::U8: return "uint8_t";
                case RetKind::I16: return "int16_t";
                case RetKind::U16: return "uint16_t";
                case RetKind::I32: return "int32_t";
                case RetKind::U32: return "uint32_t";
                case RetKind::I64: return "int64_t";
                case RetKind::U64: return "uint64_t";
                case RetKind::Ptr: return "void*";
                case RetKind::F32: return "float";
                case RetKind::F64: return "double";
                case RetKind::Struct: return nullptr;
            }
            return nullptr;
        };
        const char* ret = retName(prototype.ret);
        if (ret == nullptr) continue;
        declarations += "typedef " + std::string(ret) + " (";
        if (prototype.convention == CallingConvention::Stdcall)
            declarations += "__stdcall ";
        declarations += "*" + name + ")( ";
        if (prototype.args.empty())
            declarations += "void";
        else
        {
            for (size_t i = 0; i < prototype.args.size(); ++i)
            {
                if (i != 0) declarations += ", ";
                const char* arg = CanonicalTypeName(prototype.args[i]);
                declarations += arg != nullptr ? arg : "void*";
            }
        }
        declarations += ");\n";
    }
    return declarations;
}

ArgClass ParseSingleType(const std::string& type, const TypeRegistry* registry = nullptr)
{
    const std::string source = registry == nullptr ? "" : RegistryPreamble(*registry);
    const auto parsed = ParseCdef(source + "void __ffi_type(" + type + " value);");
    if (parsed.prototypes.size() != 1 || parsed.prototypes.front().args.size() != 1)
        BindError("ffi: expected one supported C type");
    return parsed.prototypes.front().args.front();
}

std::string TypeNameFrom(const Lode::StackValue& value, const char* api)
{
    if (value.IsString()) return value.AsString();
    if (value.IsTable())
    {
        const auto name = value.AsTable().Get("__ffiType");
        if (name.IsOk() && name.GetValue().IsString()) return name.GetValue().AsString();
    }
    BindError(std::string(api) + ": expected a C type name or descriptor");
}

TypeRegistry RegistryForState(lua_State* state)
{
    TypeRegistry registry;
    std::lock_guard lock(gTypeRegistryMutex);
    const auto found = gTypeRegistries.find(state);
    if (found != gTypeRegistries.end()) registry = found->second;
    return registry;
}

CdefParseResult ParseForState(lua_State* state, const std::string& declarations)
{
    const TypeRegistry registry = RegistryForState(state);
    return ParseCdef(RegistryPreamble(registry) + declarations);
}

bool SameLayout(const StructLayout& left, const StructLayout& right)
{
    return left.isUnion == right.isUnion && left.fields == right.fields &&
           left.fieldStructNames == right.fieldStructNames && left.fieldNames == right.fieldNames &&
           left.fieldCounts == right.fieldCounts;
}

bool SamePrototype(const Prototype& left, const Prototype& right)
{
    return left.args == right.args && left.argStructNames == right.argStructNames &&
           left.ret == right.ret && left.retStructName == right.retStructName &&
           left.convention == right.convention;
}

Lode::Value ReadScalar(const Lode::Value& storage, size_t offset, ArgClass type)
{
    auto bytes = storage.AsSpan();
    if (offset + SizeOf(type) > bytes.size()) BindError("ffi: read exceeds buffer bounds");
    const auto* p = bytes.data() + offset;
    switch (type)
    {
        case ArgClass::Bool: return Lode::Value(static_cast<double>(*p != 0));
        case ArgClass::I8: { int8_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::U8: return Lode::Value(static_cast<double>(*p));
        case ArgClass::I16: { int16_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::U16: { uint16_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::I32: { int32_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::U32: { uint32_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::I64: { int64_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::U64: { uint64_t v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::F32: { float v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(static_cast<double>(v)); }
        case ArgClass::F64: { double v; std::memcpy(&v, p, sizeof(v)); return Lode::Value(v); }
        case ArgClass::Ptr: { void* v = nullptr; std::memcpy(&v, p, sizeof(v)); return v ? Lode::Value(v) : Lode::Value(); }
        default: return Lode::Value();
    }
}

void WriteScalar(const Lode::Value& storage, size_t offset, ArgClass type, const Lode::StackValue& value)
{
    auto bytes = storage.AsSpan();
    if (offset + SizeOf(type) > bytes.size()) BindError("ffi: write exceeds buffer bounds");
    auto* p = bytes.data() + offset;
    if (type == ArgClass::Ptr)
    {
        void* pointer = nullptr; std::string error, temporary;
        if (!ExtractPointerArg(value, &pointer, &error, &temporary)) BindError("ffi: " + error);
        std::memcpy(p, &pointer, sizeof(pointer)); return;
    }
    if (!value.IsNumber() && !value.IsBoolean()) BindError("ffi: scalar value must be a number or boolean");
    const double n = value.AsNumber();
    switch (type)
    {
        case ArgClass::Bool: { const uint8_t v = n != 0; std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::I8: { const int8_t v = static_cast<int8_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::U8: { const uint8_t v = static_cast<uint8_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::I16: { const int16_t v = static_cast<int16_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::U16: { const uint16_t v = static_cast<uint16_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::I32: { const int32_t v = static_cast<int32_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::U32: { const uint32_t v = static_cast<uint32_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::I64: { const int64_t v = static_cast<int64_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::U64: { const uint64_t v = static_cast<uint64_t>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::F32: { const float v = static_cast<float>(n); std::memcpy(p, &v, sizeof(v)); break; }
        case ArgClass::F64: { const double v = n; std::memcpy(p, &v, sizeof(v)); break; }
        default: break;
    }
}

// Everything needed to invoke one bound symbol. Prepared once at bind time;
// captured by the per-function closures through a shared_ptr.
struct BoundFunction
{
    ffi_cif cif{};
    void* fn = nullptr;
    RetKind ret = RetKind::Void;
    size_t arity = 0;
    std::vector<ArgClass> args;
    std::vector<ffi_type*> argTypes;
    struct BoundStructType
    {
        ffi_type type{};
        std::vector<ffi_type*> elements;
    };
    std::vector<std::shared_ptr<BoundStructType>> argStructs;
    std::shared_ptr<BoundStructType> retStruct;
    std::vector<std::shared_ptr<BoundStructType>> ownedStructs;
};

ffi_type* FfiTypeForClass(ArgClass cls)
{
    switch (cls)
    {
        case ArgClass::F32: return &ffi_type_float;
        case ArgClass::F64: return &ffi_type_double;
        case ArgClass::Bool: return &ffi_type_uint8;
        case ArgClass::I8: return &ffi_type_sint8;
        case ArgClass::U8: return &ffi_type_uint8;
        case ArgClass::I16: return &ffi_type_sint16;
        case ArgClass::U16: return &ffi_type_uint16;
        case ArgClass::I32: return &ffi_type_sint32;
        case ArgClass::U32: return &ffi_type_uint32;
        case ArgClass::I64: return &ffi_type_sint64;
        case ArgClass::U64: return &ffi_type_uint64;
        case ArgClass::Ptr:
            return &ffi_type_pointer;
    }
    return &ffi_type_sint64;
}

const ffi_type* FfiTypeForReturn(RetKind ret)
{
    switch (ret)
    {
        case RetKind::Ptr: return &ffi_type_pointer;
        case RetKind::F32: return &ffi_type_float;
        case RetKind::F64: return &ffi_type_double;
        case RetKind::Bool: return &ffi_type_uint8;
        case RetKind::I8: return &ffi_type_sint8;
        case RetKind::U8: return &ffi_type_uint8;
        case RetKind::I16: return &ffi_type_sint16;
        case RetKind::U16: return &ffi_type_uint16;
        case RetKind::I32: return &ffi_type_sint32;
        case RetKind::U32: return &ffi_type_uint32;
        case RetKind::I64: return &ffi_type_sint64;
        case RetKind::U64: return &ffi_type_uint64;
        case RetKind::Struct: return nullptr;
        default: return &ffi_type_sint32; // Void uses a harmless return slot
    }
}

ffi_abi FfiAbiFor(CallingConvention convention)
{
    if (convention != CallingConvention::Stdcall) return FFI_DEFAULT_ABI;
#if defined(_WIN32) && !defined(_WIN64) && defined(FFI_STDCALL)
    return FFI_STDCALL;
#else
    // Microsoft x64 has one non-vectorcall ABI; the parser has already
    // rejected stdcall where that equivalence is not valid.
    return FFI_DEFAULT_ABI;
#endif
}

std::shared_ptr<BoundFunction> PrepareBoundFunction(
    const DynamicLibrary& lib, const Prototype& proto,
    const std::unordered_map<std::string, const StructLayout*>& structLayouts)
{
    auto bound = std::make_shared<BoundFunction>();

    std::string symErr;
    bound->fn = lib.Symbol(proto.name, &symErr);
    if (bound->fn == nullptr)
        BindError("ffi.load: " + symErr);

    bound->ret = proto.ret;
    bound->arity = proto.args.size();
    bound->args = proto.args;

    bound->argTypes.resize(proto.args.size());
    bound->argStructs.resize(proto.args.size());
    std::unordered_map<std::string, std::shared_ptr<BoundFunction::BoundStructType>> builtStructs;
    auto buildStruct = [&](auto&& self, const std::string& name)
        -> std::shared_ptr<BoundFunction::BoundStructType> {
        if (const auto existing = builtStructs.find(name); existing != builtStructs.end())
            return existing->second;
        const auto layout = structLayouts.find(name);
        if (layout == structLayouts.end())
            BindError("ffi.load: missing layout for struct '" + name + "'");
        auto aggregate = std::make_shared<BoundFunction::BoundStructType>();
        builtStructs.emplace(name, aggregate);
        aggregate->elements.reserve(layout->second->fields.size() + 1);
        for (size_t field = 0; field < layout->second->fields.size(); ++field)
        {
            if (layout->second->fields[field] == ArgClass::Struct)
                aggregate->elements.push_back(&self(self, layout->second->fieldStructNames[field])->type);
            else
                aggregate->elements.push_back(FfiTypeForClass(layout->second->fields[field]));
        }
        aggregate->elements.push_back(nullptr);
        aggregate->type.type = FFI_TYPE_STRUCT;
        aggregate->type.elements = aggregate->elements.data();
        return aggregate;
    };
    for (size_t i = 0; i < proto.args.size(); ++i)
    {
        if (proto.args[i] != ArgClass::Struct)
        {
            bound->argTypes[i] = FfiTypeForClass(proto.args[i]);
            continue;
        }

        auto aggregate = buildStruct(buildStruct, proto.argStructNames[i]);
        bound->argTypes[i] = &aggregate->type;
        bound->argStructs[i] = std::move(aggregate);
    }

    ffi_type* returnType = const_cast<ffi_type*>(FfiTypeForReturn(proto.ret));
    if (proto.ret == RetKind::Struct)
    {
        auto aggregate = buildStruct(buildStruct, proto.retStructName);
        returnType = &aggregate->type;
        bound->retStruct = std::move(aggregate);
    }

    for (const auto& [_, aggregate] : builtStructs)
        bound->ownedStructs.push_back(aggregate);

    if (ffi_prep_cif(&bound->cif, FfiAbiFor(proto.convention),
                     static_cast<unsigned>(proto.args.size()),
                     returnType,
                     bound->argTypes.data()) != FFI_OK)
        BindError("ffi.load: failed to prepare call interface for '" + proto.name + "'");

    return bound;
}

// Extracts one integer-class argument (integers, pointers, booleans, nil as
// NULL, strings/buffers as borrowed data pointers valid for the duration of
// the call). Returns false and fills *err on type mismatch.
bool ExtractIntArg(const Lode::StackValue& v, uint64_t* out, std::string* err,
                   std::string* stringStorage)
{
    switch (v.GetType())
    {
        case Lode::ValueType::Boolean:
            *out = v.AsBoolean() ? 1 : 0;
            return true;
        case Lode::ValueType::Integer:
            *out = static_cast<uint64_t>(v.AsInteger());
            return true;
        case Lode::ValueType::Number:
            *out = static_cast<uint64_t>(static_cast<int64_t>(v.AsNumber()));
            return true;
        case Lode::ValueType::Buffer:
        {
            const auto span = v.AsSpan();
            if (span.size() < sizeof(*out))
            {
                *err = "expected an 8-byte buffer for a 64-bit integer";
                return false;
            }
            std::memcpy(out, span.data(), sizeof(*out));
            return true;
        }
        default:
            break;
    }
    *err = "expected an integer or boolean";
    return false;
}

bool ExtractFloatArg(const Lode::StackValue& v, double* out, std::string* err)
{
    if (v.GetType() == Lode::ValueType::Number || v.GetType() == Lode::ValueType::Integer)
    {
        *out = v.AsNumber();
        return true;
    }
    *err = "expected a number";
    return false;
}

bool ExtractPointerArg(const Lode::StackValue& v, void** out, std::string* err,
                       std::string* stringStorage)
{
    switch (v.GetType())
    {
        case Lode::ValueType::Nil:
            *out = nullptr;
            return true;
        case Lode::ValueType::LightUserdata:
            *out = v.AsLightUserdata();
            return true;
        case Lode::ValueType::String:
        {
            *stringStorage = std::string(v.AsStringView());
            *out = const_cast<char*>(stringStorage->c_str());
            return true;
        }
        case Lode::ValueType::Buffer:
        {
            auto span = v.AsSpan();
            *out = span.empty() ? nullptr : span.data();
            return true;
        }
        case Lode::ValueType::Table:
        {
            const auto storage = v.AsTable().Get("__ffiBuffer");
            if (storage.IsOk() && storage.GetValue().IsBuffer())
            {
                const auto span = storage.GetValue().AsSpan();
                *out = span.empty() ? nullptr : span.data();
                return true;
            }
            const auto closed = v.AsTable().Get("__ffiClosed");
            if (closed.IsOk() && closed.GetValue().IsBoolean() && closed.GetValue().AsBoolean())
            {
                *err = "callback is closed";
                return false;
            }
            const auto pointer = v.AsTable().Get("Pointer");
            if (pointer.IsOk() && pointer.GetValue().GetType() == Lode::ValueType::LightUserdata)
            {
                *out = pointer.GetValue().AsLightUserdata();
                return true;
            }
            break;
        }
        default:
            break;
    }
    *err = "expected string, buffer, opaque pointer or nil";
    return false;
}

struct CallbackState
{
    ffi_cif cif{};
    std::vector<ArgClass> args;
    RetKind ret = RetKind::Void;
    std::vector<ffi_type*> argTypes;
    Lode::Value function;
    lua_State* luaState = nullptr;
    std::thread::id ownerThread;
    ffi_closure* closure = nullptr;
    void* code = nullptr;
    std::mutex mutex;
    std::condition_variable idle;
    size_t activeCalls = 0;
    bool closed = false;
    bool faulted = false;
    bool pendingFree = false;
    std::string lastError;
    std::atomic<uint64_t> foreignCalls{0};
    std::atomic<uint64_t> postedForeignCalls{0};
    std::atomic<uint64_t> rejectedForeignCalls{0};
    bool postForeignCalls = false;
    Lode::EventLoop* eventLoop = nullptr;
    ~CallbackState() { if (closure != nullptr) ffi_closure_free(closure); }
    bool BeginCall()
    {
        std::lock_guard lock(mutex);
        if (closed || faulted) return false;
        ++activeCalls;
        return true;
    }
    void EndCall()
    {
        std::lock_guard lock(mutex);
        if (--activeCalls == 0)
        {
            if (pendingFree && closure != nullptr)
            {
                ffi_closure_free(closure);
                closure = nullptr;
                code = nullptr;
                pendingFree = false;
            }
            idle.notify_all();
        }
    }
    void Close()
    {
        std::unique_lock lock(mutex);
        if (closed) return;
        closed = true;
        // Any callback currently running on this thread may be nested inside
        // this callback (A -> B -> A.Close). Waiting for activeCalls here
        // would deadlock that stack, so defer native closure reclamation until
        // the final EndCall instead.
        const bool reentrant = Lode::Detail::CurrentCFunctionCallContext().inForeignCallback;
        if (reentrant && activeCalls != 0)
        {
            pendingFree = true;
            return;
        }
        idle.wait(lock, [this] { return activeCalls == 0; });
        if (closure != nullptr) { ffi_closure_free(closure); closure = nullptr; code = nullptr; }
    }
    void Fault(std::string error)
    {
        std::lock_guard lock(mutex);
        if (faulted) return;
        faulted = true;
        lastError = std::move(error);
    }
    bool IsClosed()
    {
        std::lock_guard lock(mutex);
        return closed;
    }
    bool IsFaulted()
    {
        std::lock_guard lock(mutex);
        return faulted;
    }
    std::string LastError()
    {
        std::lock_guard lock(mutex);
        return lastError;
    }
};

Lode::Value CallbackArgument(ArgClass type, void* value)
{
    switch (type)
    {
        case ArgClass::Bool: return Lode::Value(static_cast<double>(*static_cast<uint8_t*>(value) != 0));
        case ArgClass::I8: return Lode::Value(static_cast<double>(*static_cast<int8_t*>(value)));
        case ArgClass::U8: return Lode::Value(static_cast<double>(*static_cast<uint8_t*>(value)));
        case ArgClass::I16: return Lode::Value(static_cast<double>(*static_cast<int16_t*>(value)));
        case ArgClass::U16: return Lode::Value(static_cast<double>(*static_cast<uint16_t*>(value)));
        case ArgClass::I32: return Lode::Value(static_cast<double>(*static_cast<int32_t*>(value)));
        case ArgClass::U32: return Lode::Value(static_cast<double>(*static_cast<uint32_t*>(value)));
        case ArgClass::I64: return Lode::Value(static_cast<double>(*static_cast<int64_t*>(value)));
        case ArgClass::U64: return Lode::Value(static_cast<double>(*static_cast<uint64_t*>(value)));
        case ArgClass::F32: return Lode::Value(static_cast<double>(*static_cast<float*>(value)));
        case ArgClass::F64: return Lode::Value(*static_cast<double*>(value));
        case ArgClass::Ptr:
        {
            void* pointer = *static_cast<void**>(value);
            return pointer == nullptr ? Lode::Value() : Lode::Value(pointer);
        }
        case ArgClass::Struct: return Lode::Value();
    }
    return Lode::Value();
}

void InvokeCallback(ffi_cif*, void* output, void** values, void* userdata)
{
    auto& callback = *static_cast<CallbackState*>(userdata);
    // Always provide the ABI zero value, including calls rejected because the
    // callback was closed or faulted before entering the protected path.
    if (output != nullptr && callback.ret != RetKind::Void)
        std::memset(output, 0, callback.cif.rtype->size);
    if (!callback.BeginCall())
        return;
    struct EndCall
    {
        CallbackState& callback;
        ~EndCall() { callback.EndCall(); }
    } endCall{callback};
    if (std::this_thread::get_id() != callback.ownerThread ||
        Lode::Detail::CurrentCFunctionCallContext().activeState == nullptr)
    {
        ++callback.foreignCalls;
        if (callback.postForeignCalls && callback.eventLoop != nullptr)
        {
            ++callback.postedForeignCalls;
            std::vector<Lode::Value> copied;
            copied.reserve(callback.args.size());
            for (size_t i = 0; i < callback.args.size(); ++i)
                copied.push_back(CallbackArgument(callback.args[i], values[i]));
            const Lode::Value function = callback.function;
            lua_State* const state = callback.luaState;
            if (!callback.eventLoop->Post([function, state, copied = std::move(copied)] {
                Lode::State vm(state);
                // The event loop may currently be resuming another Luau
                // coroutine. Route through Task so this callback gets its
                // own scheduler-owned coroutine instead of reentering Lua.
                Lode::Task::Defer(vm, function, copied);
            }))
                Lode::Logger::Error("ffi callback post rejected while event loop is closing");
        }
        else
        {
            ++callback.rejectedForeignCalls;
            Lode::Logger::Error("ffi callback invoked from a non-runtime thread");
        }
        return;
    }
    std::vector<Lode::Value> args;
    args.reserve(callback.args.size());
    for (size_t i = 0; i < callback.args.size(); ++i)
        args.push_back(CallbackArgument(callback.args[i], values[i]));
    lua_State* activeState = Lode::Detail::CurrentCFunctionCallContext().activeState;
    Lode::State vm(activeState);
    Lode::Detail::ScopedForeignCallback callbackScope(&callback);
    const auto result = callback.function.CallSingle(vm, args);
    if (result.IsError())
    {
        const std::string error = result.GetError().ErrorMessage();
        const bool wasFaulted = callback.IsFaulted();
        callback.Fault(error);
        if (!wasFaulted)
            Lode::Logger::Error("ffi callback failed: " + error);
        return;
    }
    const double number = result.GetValue().IsNumber() ? result.GetValue().AsNumber() : 0.0;
    switch (callback.ret)
    {
        case RetKind::Void: break;
        case RetKind::Bool: *static_cast<uint8_t*>(output) = number != 0.0; break;
        case RetKind::I8: *static_cast<int8_t*>(output) = static_cast<int8_t>(number); break;
        case RetKind::U8: *static_cast<uint8_t*>(output) = static_cast<uint8_t>(number); break;
        case RetKind::I16: *static_cast<int16_t*>(output) = static_cast<int16_t>(number); break;
        case RetKind::U16: *static_cast<uint16_t*>(output) = static_cast<uint16_t>(number); break;
        case RetKind::I32: *static_cast<int32_t*>(output) = static_cast<int32_t>(number); break;
        case RetKind::U32: *static_cast<uint32_t*>(output) = static_cast<uint32_t>(number); break;
        case RetKind::I64: *static_cast<int64_t*>(output) = static_cast<int64_t>(number); break;
        case RetKind::U64: *static_cast<uint64_t*>(output) = static_cast<uint64_t>(number); break;
        case RetKind::F32: *static_cast<float*>(output) = static_cast<float>(number); break;
        case RetKind::F64: *static_cast<double*>(output) = number; break;
        case RetKind::Ptr:
            if (result.GetValue().GetType() == Lode::ValueType::LightUserdata)
                *static_cast<void**>(output) = result.GetValue().AsLightUserdata();
            else if (result.GetValue().GetType() == Lode::ValueType::Buffer)
            {
                const auto span = result.GetValue().AsSpan();
                *static_cast<void**>(output) = span.empty() ? nullptr : span.data();
            }
            break;
        default: break;
    }
}

bool IsSafeLuauInteger(RetKind kind, uint64_t value)
{
    if (kind == RetKind::U64)
        return value <= kMaxSafeInteger;
    if (kind == RetKind::I64)
    {
        const int64_t signedValue = static_cast<int64_t>(value);
        return signedValue >= -static_cast<int64_t>(kMaxSafeInteger) &&
               signedValue <= static_cast<int64_t>(kMaxSafeInteger);
    }
    return true;
}

void PushResult(lua_State* L, RetKind kind, uint64_t intValue, double floatValue)
{
    switch (kind)
    {
        case RetKind::Void:
            break;
        case RetKind::Ptr:
            if (intValue == 0)
                lua_pushnil(L);
            else
                lua_pushlightuserdata(L, reinterpret_cast<void*>(intValue));
            break;
        case RetKind::Bool:
            lua_pushnumber(L, (intValue & 0xffu) == 0 ? 0.0 : 1.0);
            break;
        case RetKind::I8:
            lua_pushnumber(L, static_cast<double>(static_cast<int8_t>(intValue)));
            break;
        case RetKind::U8:
            lua_pushnumber(L, static_cast<double>(static_cast<uint8_t>(intValue)));
            break;
        case RetKind::I16:
            lua_pushnumber(L, static_cast<double>(static_cast<int16_t>(intValue)));
            break;
        case RetKind::U16:
            lua_pushnumber(L, static_cast<double>(static_cast<uint16_t>(intValue)));
            break;
        case RetKind::I32:
            lua_pushnumber(L, static_cast<double>(static_cast<int32_t>(intValue)));
            break;
        case RetKind::U32:
            lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(intValue)));
            break;
        case RetKind::I64:
            if (!IsSafeLuauInteger(kind, intValue))
            {
                void* output = lua_newbuffer(L, sizeof(intValue));
                std::memcpy(output, &intValue, sizeof(intValue));
            }
            else
                lua_pushnumber(L, static_cast<double>(static_cast<int64_t>(intValue)));
            break;
        case RetKind::U64:
            if (!IsSafeLuauInteger(kind, intValue))
            {
                void* output = lua_newbuffer(L, sizeof(intValue));
                std::memcpy(output, &intValue, sizeof(intValue));
            }
            else
                lua_pushnumber(L, static_cast<double>(intValue));
            break;
        case RetKind::F32:
        case RetKind::F64:
            lua_pushnumber(L, floatValue);
            break;
    }
}

// Configuration and object-construction functions still return a Lode::Value,
// but are installed through the N closure path as well.  This keeps the cached
// State wrapper and exception bridge used by the hot bound-symbol closures;
// only their intentionally non-hot result materialization remains boxed.
template <typename Fn>
Lode::Value CreateFfiFunctionN(Lode::State& vm, Fn&& function)
{
    return vm.CreateFastFunctionN([fn = std::forward<Fn>(function)](Lode::State& state, Lode::StackArgs args) mutable -> int {
        // FastFunctionN caches the module-registration State. Configuration
        // APIs may allocate tables/buffers, so bind them to the currently
        // executing coroutine state rather than that cached root stack.
        Lode::State current(args.RawState());
        Lode::Value result = fn(current, args);
        if (result.IsNil()) return 0;
        result.PushToLuaState(args.RawState());
        return 1;
    });
}

struct AsyncCall
{
    uv_work_t work{};
    std::shared_ptr<DynamicLibrary> library;
    std::shared_ptr<BoundFunction> bound;
    Lode::Coroutine coroutine;
    lua_State* state = nullptr;

    alignas(16) std::array<uint64_t, kMaxArity> ints{};
    std::array<void*, kMaxArity> pointers{};
    alignas(16) std::array<double, kMaxArity> f64{};
    alignas(16) std::array<float, kMaxArity> f32{};
    std::array<std::string, kMaxArity> strings;
    std::array<std::vector<uint8_t>, kMaxArity> buffers;
    std::array<void*, kMaxArity> values{};

    uint64_t intResult = 0;
    double f64Result = 0.0;
    float f32Result = 0.0f;
    std::vector<uint8_t> structResult;
    int lastError = 0;
    std::string error;
};

bool PrepareAsyncArguments(AsyncCall& call, Lode::StackArgs args, size_t firstArgument,
                           std::string* error)
{
    for (size_t i = 0; i < call.bound->arity; ++i)
    {
        const Lode::StackValue& value = args[firstArgument + i];
        const ArgClass declared = call.bound->args[i];
        if (declared == ArgClass::F32 || declared == ArgClass::F64)
        {
            double number = 0.0;
            if (!ExtractFloatArg(value, &number, error)) return false;
            if (declared == ArgClass::F32)
            {
                call.f32[i] = static_cast<float>(number);
                call.values[i] = &call.f32[i];
            }
            else
            {
                call.f64[i] = number;
                call.values[i] = &call.f64[i];
            }
        }
        else if (declared == ArgClass::Ptr)
        {
            if (value.GetType() == Lode::ValueType::Buffer)
            {
                const auto span = value.AsSpan();
                call.buffers[i].assign(span.begin(), span.end());
                call.pointers[i] = call.buffers[i].empty() ? nullptr : call.buffers[i].data();
            }
            else if (!ExtractPointerArg(value, &call.pointers[i], error, &call.strings[i]))
            {
                return false;
            }
            call.values[i] = &call.pointers[i];
        }
        else if (declared == ArgClass::Struct)
        {
            if (value.GetType() != Lode::ValueType::Buffer)
            {
                *error = "expected a buffer for struct argument";
                return false;
            }
            const auto span = value.AsSpan();
            call.buffers[i].assign(span.begin(), span.end());
            call.values[i] = call.buffers[i].data();
        }
        else
        {
            if (!ExtractIntArg(value, &call.ints[i], error, &call.strings[i])) return false;
            call.values[i] = &call.ints[i];
        }
    }
    return true;
}

void ExecuteAsync(uv_work_t* work)
{
    auto& call = *static_cast<AsyncCall*>(work->data);
    if (call.bound->ret == RetKind::Struct)
        call.structResult.resize(call.bound->retStruct->type.size);
    void* result = call.bound->ret == RetKind::Struct
                       ? static_cast<void*>(call.structResult.data())
                       : call.bound->ret == RetKind::F32
                       ? static_cast<void*>(&call.f32Result)
                       : call.bound->ret == RetKind::F64 ? static_cast<void*>(&call.f64Result)
                                                        : static_cast<void*>(&call.intResult);
    ffi_call(&call.bound->cif, FFI_FN(call.bound->fn), result, call.values.data());
#if defined(_WIN32)
    call.lastError = static_cast<int>(GetLastError());
#else
    call.lastError = errno;
#endif
}

void FinishAsync(uv_work_t* work, int status)
{
    std::unique_ptr<AsyncCall> call(static_cast<AsyncCall*>(work->data));
    Lode::State vm(call->state);
    gLastError = call->lastError;
    if (status < 0)
    {
        call->error = std::string("ffi.callAsync: ") + uv_strerror(status);
    }

    if (!call->error.empty())
    {
        const auto resumed = call->coroutine.ResumeError(call->error);
        if (resumed.IsError() && Lode::Task::IsMainThread(vm, call->coroutine.GetThreadState()))
            Lode::Task::SetMainThreadError(vm, resumed.GetError().ErrorMessage());
        return;
    }

    std::vector<Lode::Value> result;
    switch (call->bound->ret)
    {
        case RetKind::Void: break;
        case RetKind::Ptr:
            result.emplace_back(call->intResult == 0 ? Lode::Value()
                                                      : Lode::Value(reinterpret_cast<void*>(call->intResult)));
            break;
        case RetKind::Bool: result.emplace_back(static_cast<double>((call->intResult & 0xffu) != 0)); break;
        // Keep asynchronous integer results consistent with the synchronous
        // path, which uses lua_pushnumber for every integer return kind.
        case RetKind::I8: result.emplace_back(static_cast<double>(static_cast<int8_t>(call->intResult))); break;
        case RetKind::U8: result.emplace_back(static_cast<double>(static_cast<uint8_t>(call->intResult))); break;
        case RetKind::I16: result.emplace_back(static_cast<double>(static_cast<int16_t>(call->intResult))); break;
        case RetKind::U16: result.emplace_back(static_cast<double>(static_cast<uint16_t>(call->intResult))); break;
        case RetKind::I32: result.emplace_back(static_cast<double>(static_cast<int32_t>(call->intResult))); break;
        case RetKind::U32: result.emplace_back(static_cast<double>(static_cast<uint32_t>(call->intResult))); break;
        case RetKind::I64:
        case RetKind::U64:
            if (IsSafeLuauInteger(call->bound->ret, call->intResult))
                result.emplace_back(call->bound->ret == RetKind::I64
                                        ? static_cast<double>(static_cast<int64_t>(call->intResult))
                                        : static_cast<double>(call->intResult));
            else
            {
                Lode::Value buffer = vm.CreateBuffer(sizeof(call->intResult));
                std::memcpy(buffer.AsSpan().data(), &call->intResult, sizeof(call->intResult));
                result.emplace_back(std::move(buffer));
            }
            break;
        case RetKind::F32: result.emplace_back(static_cast<double>(call->f32Result)); break;
        case RetKind::F64: result.emplace_back(call->f64Result); break;
        case RetKind::Struct:
        {
            Lode::Value buffer = vm.CreateBuffer(call->structResult.size());
            std::memcpy(buffer.AsSpan().data(), call->structResult.data(), call->structResult.size());
            result.emplace_back(std::move(buffer));
            break;
        }
    }
    const auto resumed = call->coroutine.Resume(result);
    if (resumed.IsError() && Lode::Task::IsMainThread(vm, call->coroutine.GetThreadState()))
        Lode::Task::SetMainThreadError(vm, resumed.GetError().ErrorMessage());
}

// Builds the exports table for one loaded library: every parsed prototype
// becomes a bound, eagerly-resolved fastN closure with a prepared ffi plan.
Lode::Table BindLibrary(Lode::State& vm, std::shared_ptr<DynamicLibrary> lib,
                        const CdefParseResult& parsed)
{
    Lode::Table exports = vm.CreateTable();
    auto bindings = std::make_shared<std::unordered_map<std::string, std::shared_ptr<BoundFunction>>>();

    std::unordered_map<std::string, const StructLayout*> structLayouts;
    for (const StructLayout& layout : parsed.structs)
        structLayouts.emplace(layout.name, &layout);

    for (const Prototype& proto : parsed.prototypes)
    {
        auto bound = PrepareBoundFunction(*lib, proto, structLayouts);
        bindings->emplace(proto.name, bound);

        auto closure = [lib, bound](Lode::State& callVm, Lode::StackArgs args) -> int {
            lua_State* L = args.RawState();
            const size_t expected = bound->arity;
            if (args.Size() != expected)
            {
                luaL_error(L, "ffi: expects %d argument(s), got %d",
                           static_cast<int>(expected), static_cast<int>(args.Size()));
                return 0;
            }

            // Argument storage: one slot per parameter; their addresses are
            // handed to libffi. Integer-class values are stored bit-exact as
            // uint64; float/double per their declared class. The declared
            // class is recovered from the prepared cif, so the closure needs
            // no duplicate capture state.
            alignas(16) std::array<uint64_t, kMaxArity> ints{};
            std::array<void*, kMaxArity> pointers{};
            alignas(16) std::array<double, kMaxArity> flts{};
            alignas(16) std::array<float, kMaxArity> f32Args{};
            std::array<std::string, kMaxArity> stringStorage;
            std::array<void*, kMaxArity> values{};
            for (size_t i = 0; i < expected; ++i)
            {
                std::string err;
                const ArgClass declared = bound->args[i];
                if (declared == ArgClass::F32 || declared == ArgClass::F64)
                {
                    if (declared == ArgClass::F32)
                    {
                        double value = 0.0;
                        if (!ExtractFloatArg(args[i], &value, &err))
                            luaL_error(L, "ffi: argument %d: %s", static_cast<int>(i + 1),
                                       err.c_str());
                        f32Args[i] = static_cast<float>(value);
                        values[i] = &f32Args[i];
                    }
                    else
                    {
                        if (!ExtractFloatArg(args[i], &flts[i], &err))
                            luaL_error(L, "ffi: argument %d: %s", static_cast<int>(i + 1),
                                       err.c_str());
                        values[i] = &flts[i];
                    }
                }
                else if (declared == ArgClass::Ptr)
                {
                    if (!ExtractPointerArg(args[i], &pointers[i], &err, &stringStorage[i]))
                        luaL_error(L, "ffi: argument %d: %s", static_cast<int>(i + 1),
                                   err.c_str());
                    values[i] = &pointers[i];
                }
                else if (declared == ArgClass::Struct)
                {
                    if (args[i].GetType() != Lode::ValueType::Buffer)
                        luaL_error(L, "ffi: argument %d: expected a buffer for struct argument",
                                   static_cast<int>(i + 1));
                    values[i] = const_cast<uint8_t*>(args[i].AsSpan().data());
                }
                else
                {
                    if (!ExtractIntArg(args[i], &ints[i], &err, &stringStorage[i]))
                        luaL_error(L, "ffi: argument %d: %s", static_cast<int>(i + 1),
                                   err.c_str());
                    values[i] = &ints[i];
                }
            }

            alignas(16) uint64_t intResult = 0;
            alignas(16) double floatResult = 0.0;
            alignas(16) float f32Result = 0.0f;
            Lode::Value structResult;
            if (bound->ret == RetKind::Struct)
                structResult = callVm.CreateBuffer(bound->retStruct->type.size);
            void* retSlot = bound->ret == RetKind::Struct
                                ? static_cast<void*>(structResult.AsSpan().data())
                                : bound->ret == RetKind::F32
                                ? static_cast<void*>(&f32Result)
                                : bound->ret == RetKind::F64 ? static_cast<void*>(&floatResult)
                                                             : static_cast<void*>(&intResult);

            Lode::Detail::ScopedCFunctionCall cFunctionCall(L);
            ffi_call(&bound->cif, FFI_FN(bound->fn), retSlot, values.data());
            CaptureLastError();

            if (bound->ret == RetKind::Struct)
                structResult.PushToLuaState(L);
            else
                PushResult(L, bound->ret, intResult,
                           bound->ret == RetKind::F32 ? static_cast<double>(f32Result) : floatResult);
            return bound->ret == RetKind::Void ? 0 : 1;
        };

        exports.Set(proto.name, vm.CreateFastFunctionN(closure));
    }

    exports.Set("CallAsync", CreateFfiFunctionN(vm, [lib, bindings](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        const auto& execution = Lode::Detail::CurrentCFunctionCallContext();
        if (execution.inForeignCallback && !execution.callbackMayYield)
        {
            // Raise this as a Lua error at the native entry point.  Throwing a
            // C++ binding exception here crosses a nested lua_pcall while the
            // call is re-entered from a libffi callback; on some ABIs (notably
            // macOS arm64) that path is reported as an unknown C++ exception.
            // luaL_error keeps the failure in the Lua error channel and makes
            // Callback.State().lastError deterministic across platforms.
            vm.RaiseError("ffi.callAsync cannot be used from a synchronous foreign callback");
            return Lode::Value();
        }
        if (args.Size() < 1 || !args[0].IsString())
        {
            vm.RaiseError("ffi.callAsync: expected a bound function name as the first argument");
            return Lode::Value();
        }

        const std::string name = args[0].AsString();
        const auto it = bindings->find(name);
        if (it == bindings->end())
        {
            vm.RaiseError("ffi.callAsync: function '" + name + "' is not bound on this library");
            return Lode::Value();
        }

        const auto& bound = it->second;
        if (args.Size() - 1 != bound->arity)
        {
            vm.RaiseError("ffi.callAsync: '" + name + "' expects " +
                          std::to_string(bound->arity) + " argument(s), got " +
                          std::to_string(args.Size() - 1));
            return Lode::Value();
        }

        auto call = std::make_unique<AsyncCall>();
        call->library = lib;
        call->bound = bound;
        call->state = vm.GetLuaState();
        call->coroutine = Lode::Coroutine(call->state);
        call->work.data = call.get();
        if (!PrepareAsyncArguments(*call, args, 1, &call->error))
        {
            vm.RaiseError("ffi.callAsync: " + call->error);
            return Lode::Value();
        }

        const int queued = uv_queue_work(vm.GetEventLoop().GetUVLoop(), &call->work, ExecuteAsync, FinishAsync);
        if (queued < 0)
        {
            vm.RaiseError(std::string("ffi.callAsync: ") + uv_strerror(queued));
            return Lode::Value();
        }

        call.release();
        vm.YieldThread();
        return Lode::Value();
    }));

    // Optional explicit early release. Bound closures capture the shared_ptr,
    // so calling them afterwards is still memory-safe; the underlying handle
    // simply stays resident until the VM collects them.
    auto close = CreateFfiFunctionN(vm, [lib](Lode::State&, Lode::StackArgs) -> Lode::Value {
        lib->Close();
        return Lode::Value();
    });
    exports.Set("Close", close);

    return exports;
}

} // namespace

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    auto parseLoadOptions = [](Lode::StackArgs args) -> DynamicLibraryOptions {
        DynamicLibraryOptions options;
        if (args.Size() < 3 || args[2].IsNil()) return options;
        if (!args[2].IsTable())
            BindError("ffi.load: options must be a table or nil");

        const Lode::Table table = args[2].AsTable();
        for (const std::string& key : table.GetKeys())
        {
            if (key != "SearchDllDirectory")
                BindError("FFI.Load: unknown option '" + key + "'");
        }

        const auto value = table.Get("SearchDllDirectory");
        if (value.IsOk() && !value.GetValue().IsNil())
        {
            if (!value.GetValue().IsBoolean())
                BindError("FFI.Load: options.SearchDllDirectory must be a boolean");
            options.searchDllDirectory = value.GetValue().AsBoolean();
        }
        return options;
    };

    // ffi.load(name [, declarations]) -> library table
    //
    // Loads a dynamic library and optionally parses/binds a cdef subset of C
    // declarations in one step:
    //
    //   local user32 = ffi.load("user32", [[
    //       int MessageBoxW(void* hwnd, const char* text, const char* caption, unsigned flags);
    //   ]])
    //   user32.MessageBoxW(nil, "hi", "t", 0)
    exports.Set("Load", CreateFfiFunctionN(vm, [parseLoadOptions](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || !args[0].IsString())
            BindError("ffi.load: expected a library name string");
        if (args.Size() > 3)
            BindError("ffi.load: expected name, optional declarations, and optional options");

        const std::string name = args[0].AsString();
        const DynamicLibraryOptions options = parseLoadOptions(args);

        std::shared_ptr<DynamicLibrary> lib;
        if (args.Size() >= 2 && !args[1].IsNil())
        {
            if (!args[1].IsString())
                BindError("ffi.load: declarations must be a string");
            const std::string decls = args[1].AsString();

            // Parse before opening so syntax errors never leave a half-loaded
            // library behind. Exceptions propagate to the closure wrapper,
            // which converts them into Luau errors on the same code path as
            // every other native callback.
            std::string openErr;
            lib = DynamicLibrary::Open(name, options, &openErr);
            if (lib == nullptr)
                BindError("ffi.load: cannot open '" + name + "': " + openErr);

            return Lode::Value(BindLibrary(vm, std::move(lib), ParseForState(vm.GetMainThread(), decls)));
        }

        std::string openErr;
        lib = DynamicLibrary::Open(name, options, &openErr);
        if (lib == nullptr)
            BindError("ffi.load: cannot open '" + name + "': " + openErr);
        return Lode::Value(BindLibrary(vm, std::move(lib), {}));
    }));

    // ffi.C(declarations) binds symbols visible from the current process.
    // It is intentionally a function rather than a permanent table: each
    // declaration block is parsed and resolved eagerly like ffi.load.
    exports.Set("C", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsString())
            BindError("ffi.C: declarations must be a string");

        std::string openErr;
        const auto lib = DynamicLibrary::OpenSelf(&openErr);
        if (lib == nullptr)
            BindError("ffi.C: cannot open the current process: " + openErr);
        return Lode::Value(BindLibrary(callVm, lib, ParseForState(callVm.GetMainThread(), args[0].AsString())));
    }));

    exports.Set("LastError", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 0)
            BindError("ffi.lastError: expected no arguments");
        return Lode::Value(static_cast<double>(gLastError));
    }));

    exports.Set("CDef", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsString()) BindError("ffi.cdef: declarations must be a string");
        const std::string source = args[0].AsString();
        TypeRegistry existing;
        {
            std::lock_guard lock(gTypeRegistryMutex);
            const auto found = gTypeRegistries.find(callVm.GetMainThread());
            if (found != gTypeRegistries.end()) existing = found->second;
        }
        // The preamble lets new declarations refer to types already registered
        // for this State.  A compatible redefinition, however, cannot be
        // parsed together with its existing declaration by the C parser.  In
        // that case parse the submitted declaration independently and let the
        // registry below perform the semantic compatibility check.
        CdefParseResult parsed;
        try
        {
            parsed = ParseCdef(RegistryPreamble(existing) + source);
        }
        catch (const std::runtime_error&)
        {
            parsed = ParseCdef(source);
        }
        std::lock_guard lock(gTypeRegistryMutex);
        auto& registry = gTypeRegistries[callVm.GetMainThread()];
        for (const auto& layout : parsed.structs)
        {
            RegisteredStruct item{layout, LayoutFor(layout)};
            const auto found = registry.structs.find(layout.name);
            if (found == registry.structs.end()) registry.structs.emplace(layout.name, std::move(item));
            else if (!SameLayout(found->second.layout, layout))
                BindError("ffi.cdef: incompatible redefinition of type '" + layout.name + "'");
        }
        for (const auto& [name, type] : parsed.aliases)
        {
            const auto found = registry.aliases.find(name);
            if (found == registry.aliases.end()) registry.aliases.emplace(name, type);
            else if (found->second != type)
                BindError("ffi.cdef: incompatible redefinition of alias '" + name + "'");
        }
        for (const auto& [name, prototype] : parsed.functionTypes)
        {
            const auto found = registry.functionTypes.find(name);
            if (found == registry.functionTypes.end())
                registry.functionTypes.emplace(name, prototype);
            else if (!SamePrototype(found->second, prototype))
                BindError("ffi.cdef: incompatible redefinition of function type '" + name + "'");
        }
        return Lode::Value();
    }));

    exports.Set("TypeOf", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsString()) BindError("FFI.TypeOf: expected a type name");
        const std::string name = args[0].AsString();
        const TypeRegistry registry = RegistryForState(callVm.GetMainThread());
        Lode::Table descriptor = callVm.CreateTable();
        descriptor.Set("Name", Lode::Value(name));
        descriptor.Set("__ffiType", Lode::Value(name));
        if (const auto found = registry.structs.find(name); found != registry.structs.end())
        {
            descriptor.Set("Size", Lode::Value(static_cast<double>(found->second.raw.size)));
            descriptor.Set("Align", Lode::Value(static_cast<double>(found->second.raw.alignment)));
            Lode::Table offsets = callVm.CreateTable();
            for (const auto& [field, offset] : found->second.raw.offsets)
                offsets.Set(field, Lode::Value(static_cast<double>(offset)));
            descriptor.Set("Offset", offsets);
        }
        else if (registry.functionTypes.contains(name))
        {
            descriptor.Set("Size", Lode::Value(static_cast<double>(sizeof(void*))));
            descriptor.Set("Align", Lode::Value(static_cast<double>(alignof(void*))));
            descriptor.Set("Offset", callVm.CreateTable());
            descriptor.Set("__ffiFunction", Lode::Value(true));
        }
        else
        {
            const ArgClass type = registry.aliases.contains(name) ? registry.aliases.at(name) : ParseSingleType(name, &registry);
            descriptor.Set("Size", Lode::Value(static_cast<double>(SizeOf(type))));
            descriptor.Set("Align", Lode::Value(static_cast<double>(AlignOf(type))));
            descriptor.Set("Offset", callVm.CreateTable());
        }
        return Lode::Value(descriptor);
    }));

    exports.Set("AlignOf", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1) BindError("FFI.AlignOf: expected a type name or descriptor");
        const std::string name = TypeNameFrom(args[0], "FFI.AlignOf");
        const TypeRegistry registry = RegistryForState(callVm.GetMainThread());
        if (const auto found = registry.structs.find(name); found != registry.structs.end())
            return Lode::Value(static_cast<double>(found->second.raw.alignment));
        const ArgClass type = registry.aliases.contains(name) ? registry.aliases.at(name) : ParseSingleType(name, &registry);
        return Lode::Value(static_cast<double>(AlignOf(type)));
    }));

    // ffi.new(bytes) / ffi.new("type") returns raw, zero-initialized
    // Luau-owned memory. The cdef author controls its layout and passes the
    // buffer to pointer parameters without wrapper ownership machinery.
    exports.Set("new", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || args.Size() > 2)
            BindError("ffi.new: expected a byte size number or C type string");
        if (args[0].IsString() || args[0].IsTable())
        {
            const std::string name = TypeNameFrom(args[0], "FFI.new");
            RegisteredStruct registered;
            {
                std::lock_guard lock(gTypeRegistryMutex);
                const auto state = gTypeRegistries.find(callVm.GetMainThread());
                if (state != gTypeRegistries.end())
                {
                    const auto found = state->second.structs.find(name);
                    if (found != state->second.structs.end()) registered = found->second;
                }
            }
            if (registered.raw.size == 0)
            {
                std::lock_guard lock(gTypeRegistryMutex);
                const auto state = gTypeRegistries.find(callVm.GetMainThread());
                if (state != gTypeRegistries.end())
                {
                    const auto alias = state->second.aliases.find(name);
                    if (alias != state->second.aliases.end()) return callVm.CreateBuffer(SizeOf(alias->second));
                }
                return callVm.CreateBuffer(ParseTypeSize(name));
            }
            Lode::Value storage = callVm.CreateBuffer(registered.raw.size);
            auto keepAlive = std::make_shared<std::unordered_map<std::string, Lode::Value>>();
            Lode::Table object = callVm.CreateTable();
            object.Set("__ffiBuffer", storage);
            object.Set("__ffiType", Lode::Value(name));
            Lode::Metatable meta(callVm);
            meta.SetIndexFunction([storage, registered](Lode::State&, Lode::Value key) -> Lode::Value {
                if (!key.IsString()) return Lode::Value();
                const auto offset = registered.raw.offsets.find(key.AsString());
                if (offset == registered.raw.offsets.end()) return Lode::Value();
                size_t index = 0;
                for (; index < registered.layout.fieldNames.size(); ++index)
                    if (registered.layout.fieldNames[index] == key.AsString()) break;
                if (index == registered.layout.fieldNames.size() || registered.layout.fieldCounts[index] != 1)
                    return Lode::Value();
                size_t field = 0; for (size_t n = 0; n < index; ++n) field += registered.layout.fieldCounts[n];
                return ReadScalar(storage, offset->second, registered.layout.fields[field]);
            });
            meta.SetNewIndexFunction([storage, registered, keepAlive](Lode::State& fieldVm, Lode::Value key, Lode::Value value) {
                if (!key.IsString()) BindError("ffi struct field name must be a string");
                const auto offset = registered.raw.offsets.find(key.AsString());
                if (offset == registered.raw.offsets.end()) BindError("ffi struct: unknown field '" + key.AsString() + "'");
                size_t index = 0; for (; index < registered.layout.fieldNames.size(); ++index) if (registered.layout.fieldNames[index] == key.AsString()) break;
                if (registered.layout.fieldCounts[index] != 1) BindError("ffi struct: array fields require raw buffer access");
                size_t field = 0; for (size_t n = 0; n < index; ++n) field += registered.layout.fieldCounts[n];
                const ArgClass type = registered.layout.fields[field];
                auto bytes = storage.AsSpan(); auto* p = bytes.data() + offset->second;
                if (type == ArgClass::Ptr)
                {
                    void* pointer = nullptr;
                    Lode::Value retained = value;
                    if (value.GetType() == Lode::ValueType::LightUserdata) pointer = value.AsLightUserdata();
                    else if (value.IsBuffer()) { auto span = value.AsSpan(); pointer = span.empty() ? nullptr : span.data(); }
                    else if (value.IsString())
                    {
                        const std::string text = value.AsString();
                        retained = fieldVm.CreateBuffer(text.size() + 1);
                        auto retainedBytes = retained.AsSpan();
                        std::memcpy(retainedBytes.data(), text.data(), text.size());
                        retainedBytes[text.size()] = 0;
                        pointer = retainedBytes.data();
                    }
                    else if (value.IsTable())
                    {
                        const auto closed = value.AsTable().Get("__ffiClosed");
                        if (closed.IsOk() && closed.GetValue().IsBoolean() && closed.GetValue().AsBoolean())
                            BindError("ffi struct: pointer field callback is closed");
                        const auto v = value.AsTable().Get("Pointer");
                        if (v.IsOk() && v.GetValue().GetType() == Lode::ValueType::LightUserdata)
                            pointer = v.GetValue().AsLightUserdata();
                    }
                    else if (!value.IsNil()) BindError("ffi struct: pointer field requires pointer, string, buffer, callback, or nil");
                    std::memcpy(p, &pointer, sizeof(pointer)); (*keepAlive)[key.AsString()] = retained; return;
                }
                if (!value.IsNumber() && !value.IsBoolean()) BindError("ffi struct: scalar field requires a number");
                const double number = value.AsNumber();
                switch (type)
                {
                    case ArgClass::Bool: { const uint8_t v = number != 0; std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::I8: { const int8_t v = static_cast<int8_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::U8: { const uint8_t v = static_cast<uint8_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::I16: { const int16_t v = static_cast<int16_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::U16: { const uint16_t v = static_cast<uint16_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::I32: { const int32_t v = static_cast<int32_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::U32: { const uint32_t v = static_cast<uint32_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::I64: { const int64_t v = static_cast<int64_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::U64: { const uint64_t v = static_cast<uint64_t>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::F32: { const float v = static_cast<float>(number); std::memcpy(p, &v, sizeof(v)); break; }
                    case ArgClass::F64: { const double v = number; std::memcpy(p, &v, sizeof(v)); break; }
                    default: break;
                }
            });
            object.SetMetatable(meta);
            if (args.Size() == 2 && !args[1].IsNil())
            {
                if (!args[1].IsTable()) BindError("ffi.new: initializer must be a table");
                for (const auto& key : args[1].AsTable().GetKeys())
                {
                    const auto value = args[1].AsTable().Get(key); if (value.IsOk())
                    {
                        const auto setter = meta.GetTable().Get("__newindex");
                        if (setter.IsError()) BindError("ffi.new: cannot initialize typed struct");
                        const auto set = setter.GetValue().Call(callVm, Lode::Value(object), Lode::Value(key), value.GetValue());
                        if (set.IsError()) BindError("ffi.new: " + set.GetError().ErrorMessage());
                    }
                }
            }
            return Lode::Value(object);
        }
        if (args[0].IsNumber())
        {
            const double size = args[0].AsNumber();
            if (size < 0)
                BindError("ffi.new: byte size must not be negative");
            return callVm.CreateBuffer(static_cast<size_t>(size));
        }
        BindError("ffi.new: expected a byte size number or C type string");
    }));

    exports.Set("SizeOf", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1) BindError("ffi.sizeof: expected one C type string, descriptor, or typed value");
        if (args[0].IsString() || args[0].IsTable())
        {
            const std::string name = TypeNameFrom(args[0], "FFI.SizeOf");
            std::lock_guard lock(gTypeRegistryMutex);
            const auto state = gTypeRegistries.find(callVm.GetMainThread());
            if (state != gTypeRegistries.end())
            {
                if (const auto found = state->second.structs.find(name); found != state->second.structs.end())
                    return Lode::Value(static_cast<double>(found->second.raw.size));
                if (const auto alias = state->second.aliases.find(name); alias != state->second.aliases.end())
                    return Lode::Value(static_cast<double>(SizeOf(alias->second)));
            }
            return Lode::Value(static_cast<double>(ParseTypeSize(name)));
        }
        Lode::Value storage;
        if (ExtractTypedBuffer(args[0], &storage)) return Lode::Value(static_cast<double>(storage.AsSpan().size()));
        BindError("ffi.sizeof: expected one C type string or typed value");
    }));

    exports.Set("Struct", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsString())
            BindError("ffi.struct: expected C field declarations as a string");
        return callVm.CreateBuffer(ParseStructLayout(args[0].AsString()).size);
    }));

    exports.Set("Union", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsString())
            BindError("ffi.union: expected C field declarations as a string");
        return callVm.CreateBuffer(ParseUnionLayout(args[0].AsString()).size);
    }));

    exports.Set("OffsetOf", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 2 || !args[1].IsString())
            BindError("FFI.OffsetOf: expected aggregate type and field name");
        const std::string name = TypeNameFrom(args[0], "FFI.OffsetOf");
        const TypeRegistry registry = RegistryForState(callVm.GetMainThread());
        const auto type = registry.structs.find(name);
        if (type == registry.structs.end()) BindError("FFI.OffsetOf: type is not a registered aggregate");
        const auto field = type->second.raw.offsets.find(args[1].AsString());
        if (field == type->second.raw.offsets.end())
            BindError("FFI.OffsetOf: unknown field '" + args[1].AsString() + "'");
        return Lode::Value(static_cast<double>(field->second));
    }));

    // Address helpers moved to @address. Keep their implementation compiled
    // during the transition, but never expose the legacy surface.
    if (false)
    {
    // Copies bytes out of caller-owned C memory. These helpers deliberately
    // do not track ownership or validate raw addresses: FFI pointers are a
    // low-level contract, while the returned string/buffer is Luau-owned.
    exports.Set("string", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || args.Size() > 2)
            BindError("ffi.string: expected a pointer and optional byte count");
        void* pointer = nullptr;
        std::string storage;
        std::string error;
        if (!ExtractPointerArg(args[0], &pointer, &error, &storage) || pointer == nullptr)
            BindError("ffi.string: expected a non-NULL pointer, string, or buffer");
        if (args.Size() == 1)
            return Lode::Value(std::string(static_cast<const char*>(pointer)));
        if (args[1].GetType() != Lode::ValueType::Number &&
            args[1].GetType() != Lode::ValueType::Integer)
            BindError("ffi.string: byte count must be a number");
        const size_t bytes = static_cast<size_t>(args[1].AsNumber());
        return Lode::Value(std::string(static_cast<const char*>(pointer), bytes));
    }));

    exports.Set("copy", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 2)
            BindError("ffi.copy: expected a pointer and byte count");
        void* pointer = nullptr;
        std::string storage;
        std::string error;
        if (!ExtractPointerArg(args[0], &pointer, &error, &storage) || pointer == nullptr)
            BindError("ffi.copy: expected a non-NULL pointer, string, or buffer");
        if (args[1].GetType() != Lode::ValueType::Number &&
            args[1].GetType() != Lode::ValueType::Integer)
            BindError("ffi.copy: byte count must be a number");
        const size_t bytes = static_cast<size_t>(args[1].AsNumber());
        Lode::Value output = callVm.CreateBuffer(bytes);
        std::memcpy(output.AsSpan().data(), pointer, bytes);
        return output;
    }));

    exports.Set("wstring", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
#if defined(_WIN32)
        if (args.Size() < 1 || args.Size() > 2)
            BindError("ffi.wstring: expected a UTF-16 pointer and optional code-unit count");
        void* pointer = nullptr;
        std::string storage;
        std::string error;
        if (!ExtractPointerArg(args[0], &pointer, &error, &storage) || pointer == nullptr)
            BindError("ffi.wstring: expected a non-NULL UTF-16 pointer");
        const wchar_t* wide = static_cast<const wchar_t*>(pointer);
        int units = 0;
        if (args.Size() == 1)
            units = static_cast<int>(std::wcslen(wide));
        else
        {
            if (!args[1].IsNumber()) BindError("ffi.wstring: code-unit count must be a number");
            units = static_cast<int>(args[1].AsNumber());
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units,
                                              nullptr, 0, nullptr, nullptr);
        if (bytes == 0 && units != 0) BindError("ffi.wstring: invalid UTF-16 input");
        std::string utf8(static_cast<size_t>(bytes), '\0');
        if (bytes != 0)
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, units,
                                utf8.data(), bytes, nullptr, nullptr);
        return Lode::Value(utf8);
#else
        BindError("ffi.wstring: UTF-16 helpers are only available on Windows");
#endif
    }));

    exports.Set("wbuffer", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
#if defined(_WIN32)
        if (args.Size() != 1 || !args[0].IsString())
            BindError("ffi.wbuffer: expected one UTF-8 string");
        const std::string text = args[0].AsString();
        const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
        if (units == 0 && !text.empty()) BindError("ffi.wbuffer: invalid UTF-8 input");
        Lode::Value output = callVm.CreateBuffer((static_cast<size_t>(units) + 1) * sizeof(wchar_t));
        auto* wide = reinterpret_cast<wchar_t*>(output.AsSpan().data());
        if (units != 0)
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                wide, units);
        wide[units] = L'\0';
        return output;
#else
        BindError("ffi.wbuffer: UTF-16 helpers are only available on Windows");
#endif
    }));

    // Forms a raw address without attaching lifetime or bounds metadata. It
    // is intentionally just byte-address arithmetic for low-level callers.
    exports.Set("ptr", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || args.Size() > 2)
            BindError("ffi.ptr: expected a pointer-like value and optional byte offset");
        void* pointer = nullptr;
        std::string storage;
        std::string error;
        if (!ExtractPointerArg(args[0], &pointer, &error, &storage))
            BindError("ffi.ptr: " + error);
        intptr_t offset = 0;
        if (args.Size() == 2)
        {
            if (!args[1].IsNumber())
                BindError("ffi.ptr: byte offset must be a number");
            offset = static_cast<intptr_t>(args[1].AsNumber());
        }
        const uintptr_t address = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(pointer)) + offset;
        return address == 0 ? Lode::Value() : Lode::Value(reinterpret_cast<void*>(address));
    }));

    exports.Set("addr", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 1 || args.Size() > 2) BindError("ffi.addr: expected a pointer-like value and optional byte offset");
        void* pointer=nullptr; std::string error;
        if (!ExtractStrictPointer(args[0], &pointer, &error)) BindError("ffi.addr: " + error);
        const size_t offset = args.Size() == 2 ? StrictOffset(args[1], "ffi.addr") : 0;
        StrictRange(args[0], offset, 1, "ffi.addr");
        return Lode::Value(StrictAddress(pointer, offset, 1, "ffi.addr"));
    }));
    exports.Set("cast", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size()!=2 || !args[0].IsString()) BindError("ffi.cast: expected a C type and opaque pointer");
        const TypeRegistry registry = RegistryForState(args.RawState());
        if (ParseSingleType(args[0].AsString(), &registry) != ArgClass::Ptr) BindError("ffi.cast: target type must be a pointer type");
        void* pointer=nullptr; std::string error; if(!ExtractStrictPointer(args[1], &pointer, &error)) BindError("ffi.cast: "+error);
        return Lode::Value(StrictAddress(pointer, 0, 1, "ffi.cast"));
    }));
    exports.Set("read", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size()<2 || args.Size()>3 || !args[1].IsString()) BindError("ffi.read: expected pointer, C type, and optional offset");
        void* pointer=nullptr; std::string error; if(!ExtractStrictPointer(args[0], &pointer, &error) || !pointer) BindError("ffi.read: expected non-NULL pointer");
        const TypeRegistry registry = RegistryForState(args.RawState());
        const ArgClass type=ParseSingleType(args[1].AsString(), &registry); if(type==ArgClass::Struct) BindError("ffi.read: aggregate types are not supported");
        const size_t offset = args.Size()==3 ? StrictOffset(args[2], "ffi.read") : 0;
        StrictRange(args[0], offset, SizeOf(type), "ffi.read");
        void* address = StrictAddress(pointer, offset, AlignOf(type), "ffi.read");
        // Materialize through a bounded temporary to avoid pretending foreign memory has Luau buffer bounds.
        Lode::Value copy=Lode::State(args.RawState()).CreateBuffer(SizeOf(type)); std::memcpy(copy.AsSpan().data(), address, SizeOf(type)); return ReadScalar(copy,0,type);
    }));
    exports.Set("write", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size()<3 || args.Size()>4 || !args[1].IsString()) BindError("ffi.write: expected pointer, C type, value, and optional offset");
        void* pointer=nullptr; std::string error; if(!ExtractStrictPointer(args[0], &pointer, &error) || !pointer) BindError("ffi.write: expected non-NULL pointer");
        const TypeRegistry registry = RegistryForState(args.RawState());
        const ArgClass type=ParseSingleType(args[1].AsString(), &registry); if(type==ArgClass::Struct) BindError("ffi.write: aggregate types are not supported");
        const size_t offset = args.Size()==4 ? StrictOffset(args[3], "ffi.write") : 0;
        StrictRange(args[0], offset, SizeOf(type), "ffi.write");
        void* target=StrictAddress(pointer, offset, AlignOf(type), "ffi.write"); Lode::Value temp=Lode::State(args.RawState()).CreateBuffer(SizeOf(type)); WriteScalar(temp,0,type,args[2]); std::memcpy(target,temp.AsSpan().data(),SizeOf(type)); return Lode::Value(pointer);
    }));
    exports.Set("readPointer", CreateFfiFunctionN(vm, [](Lode::State& state, Lode::StackArgs args) -> Lode::Value {
        if(args.Size()<1 || args.Size()>2) BindError("ffi.readPointer: expected pointer and optional offset");
        void* pointer=nullptr; std::string error; if(!ExtractStrictPointer(args[0],&pointer,&error)||!pointer) BindError("ffi.readPointer: expected non-NULL pointer");
        const size_t offset = args.Size()==2 ? StrictOffset(args[1], "ffi.readPointer") : 0;
        StrictRange(args[0], offset, sizeof(void*), "ffi.readPointer");
        void* address = StrictAddress(pointer, offset, alignof(void*), "ffi.readPointer");
        Lode::Value tmp=state.CreateBuffer(sizeof(void*)); std::memcpy(tmp.AsSpan().data(),address,sizeof(void*)); return ReadScalar(tmp,0,ArgClass::Ptr);
    }));
    exports.Set("writePointer", CreateFfiFunctionN(vm, [](Lode::State& state, Lode::StackArgs args) -> Lode::Value {
        if(args.Size()!=3) BindError("ffi.writePointer: expected pointer, offset, and pointer value");
        void* pointer=nullptr; std::string error; if(!ExtractStrictPointer(args[0],&pointer,&error)||!pointer) BindError("ffi.writePointer: expected non-NULL pointer");
        const size_t offset = StrictOffset(args[1], "ffi.writePointer");
        StrictRange(args[0], offset, sizeof(void*), "ffi.writePointer");
        void* address = StrictAddress(pointer, offset, alignof(void*), "ffi.writePointer");
        Lode::Value tmp=state.CreateBuffer(sizeof(void*)); WriteScalar(tmp,0,ArgClass::Ptr,args[2]); std::memcpy(address,tmp.AsSpan().data(),sizeof(void*)); return Lode::Value(pointer);
    }));

    exports.Set("fill", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 3)
            BindError("ffi.fill: expected destination, byte value, and byte count");
        void* destination = nullptr;
        std::string storage;
        std::string error;
        if (!ExtractPointerArg(args[0], &destination, &error, &storage) || destination == nullptr)
            BindError("ffi.fill: expected a non-NULL destination pointer");
        if (!args[1].IsNumber() || !args[2].IsNumber())
            BindError("ffi.fill: byte value and count must be numbers");
        std::memset(destination, static_cast<int>(args[1].AsNumber()),
                    static_cast<size_t>(args[2].AsNumber()));
        return Lode::Value(destination);
    }));

    exports.Set("move", CreateFfiFunctionN(vm, [](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 3)
            BindError("ffi.move: expected destination, source, and byte count");
        void* destination = nullptr;
        void* source = nullptr;
        std::string destinationStorage;
        std::string sourceStorage;
        std::string error;
        if (!ExtractPointerArg(args[0], &destination, &error, &destinationStorage) || destination == nullptr)
            BindError("ffi.move: expected a non-NULL destination pointer");
        if (!ExtractPointerArg(args[1], &source, &error, &sourceStorage) || source == nullptr)
            BindError("ffi.move: expected a non-NULL source pointer");
        if (!args[2].IsNumber())
            BindError("ffi.move: byte count must be a number");
        std::memmove(destination, source, static_cast<size_t>(args[2].AsNumber()));
        return Lode::Value(destination);
    }));
    }

    exports.Set("Callback", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if ((args.Size() != 2 && args.Size() != 3) || (!args[0].IsString() && !args[0].IsTable()) || !args[1].IsFunction())
            BindError("ffi.callback: expected declaration, function, and optional options");
        std::string declaration = TypeNameFrom(args[0], "FFI.Callback");
        // Callback declarations may omit a C function name. Give this
        // standalone declaration an internal name before using the ordinary
        // function parser; named declarations are preserved unchanged.
        CdefParseResult parsed;
        Prototype registeredType;
        bool useRegisteredType = false;
        if (declaration.find('(') == std::string::npos)
        {
            std::lock_guard lock(gTypeRegistryMutex);
            const auto state = gTypeRegistries.find(callVm.GetMainThread());
            if (state != gTypeRegistries.end())
            {
                const auto found = state->second.functionTypes.find(declaration);
                if (found != state->second.functionTypes.end()) { registeredType = found->second; useRegisteredType = true; }
            }
            if (!useRegisteredType) BindError("ffi.callback: unknown function type '" + declaration + "'");
        }
        if (!useRegisteredType) try { parsed = ParseCdef(declaration); }
        catch (const std::runtime_error&)
        {
            const size_t open = declaration.find('(');
            if (open == std::string::npos) throw;
            declaration.insert(open, " __ffi_callback");
            parsed = ParseCdef(declaration);
        }
        if (!useRegisteredType && parsed.prototypes.size() != 1)
            BindError("ffi.callback: declaration must contain exactly one function");
        const Prototype& prototype = useRegisteredType ? registeredType : parsed.prototypes.front();
        if (prototype.ret == RetKind::Struct)
            BindError("ffi.callback: struct returns are not supported");
        for (ArgClass type : prototype.args)
            if (type == ArgClass::Struct)
                BindError("ffi.callback: struct arguments are not supported");

        auto callback = std::make_shared<CallbackState>();
        callback->args = prototype.args;
        callback->ret = prototype.ret;
        callback->function = Lode::Value::FromLuaState(args.RawState(), 2);
        callback->luaState = callVm.GetLuaState();
        callback->ownerThread = std::this_thread::get_id();
        callback->eventLoop = &callVm.GetEventLoop();
        if (args.Size() == 3 && !args[2].IsNil())
        {
            if (!args[2].IsTable()) BindError("ffi.callback: options must be a table");
            const auto mode = args[2].AsTable().Get("ForeignThread");
            for (const auto& key : args[2].AsTable().GetKeys())
                if (key != "ForeignThread") BindError("FFI.Callback: unknown option '" + key + "'");
            if (mode.IsOk() && !mode.GetValue().IsNil())
            {
                if (!mode.GetValue().IsString() || mode.GetValue().AsString() != "post")
                    BindError("FFI.Callback: options.ForeignThread must be 'post'");
                callback->postForeignCalls = true;
            }
        }
        for (ArgClass type : prototype.args)
            callback->argTypes.push_back(FfiTypeForClass(type));
        if (ffi_prep_cif(&callback->cif, FfiAbiFor(prototype.convention),
                         static_cast<unsigned>(callback->argTypes.size()),
                         const_cast<ffi_type*>(FfiTypeForReturn(callback->ret)),
                         callback->argTypes.data()) != FFI_OK)
            BindError("ffi.callback: failed to prepare callback ABI");
        callback->closure = static_cast<ffi_closure*>(ffi_closure_alloc(sizeof(ffi_closure), &callback->code));
        if (callback->closure == nullptr)
            BindError("ffi.callback: failed to allocate closure");
        if (ffi_prep_closure_loc(callback->closure, &callback->cif, InvokeCallback,
                                 callback.get(), callback->code) != FFI_OK)
            BindError("ffi.callback: failed to prepare closure");

        Lode::Table handle = callVm.CreateTable();
        handle.Set("Pointer", Lode::Value(callback->code));
        const Lode::Value close = CreateFfiFunctionN(callVm, [callback, handle](Lode::State&, Lode::StackArgs) mutable -> Lode::Value {
            callback->Close();
            handle.Set("__ffiClosed", Lode::Value(true));
            handle.Set("Pointer", Lode::Value());
            return Lode::Value();
        });
        handle.Set("Close", close);
        handle.Set("ForeignCalls", CreateFfiFunctionN(callVm, [callback](Lode::State&, Lode::StackArgs) -> Lode::Value {
            return Lode::Value(static_cast<double>(callback->foreignCalls.load()));
        }));
        handle.Set("State", CreateFfiFunctionN(callVm, [callback](Lode::State& state, Lode::StackArgs args) -> Lode::Value {
            if (args.Size() != 0) BindError("ffi callback state: expected no arguments");
            Lode::Table snapshot = state.CreateTable();
            snapshot.Set("closed", Lode::Value(callback->IsClosed()));
            snapshot.Set("faulted", Lode::Value(callback->IsFaulted()));
            snapshot.Set("foreignCalls", Lode::Value(static_cast<double>(callback->foreignCalls.load())));
            snapshot.Set("postedForeignCalls", Lode::Value(static_cast<double>(callback->postedForeignCalls.load())));
            snapshot.Set("rejectedForeignCalls", Lode::Value(static_cast<double>(callback->rejectedForeignCalls.load())));
            snapshot.Set("lastError", Lode::Value(callback->LastError()));
            return Lode::Value(snapshot);
        }));
        return Lode::Value(handle);
    }));

    exports.Set("Post", CreateFfiFunctionN(vm, [](Lode::State& callVm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() != 1 || !args[0].IsFunction()) BindError("ffi.post: expected one function");
        const Lode::Value fn = args[0].ToValue();
        if (!callVm.GetEventLoop().Post([state = callVm.GetMainThread(), fn] {
            Lode::State vm(state);
            // The loop can be servicing another Luau coroutine here. Queue the
            // work through Task to avoid reentering that active Lua stack.
            Lode::Task::Defer(vm, fn, {});
        })) BindError("ffi.post: event loop is closing");
        return Lode::Value();
    }));

    return {exports};
}
