// Deterministic shared-library fixture for modules/ffi.
#include "Lode/Module.hpp"

#include <cstdint>
#include <atomic>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#endif

#if defined(_WIN32)
#define FFI_FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define FFI_FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C" std::int32_t ffi_fixture_dependency_scale(std::int32_t value);

struct FixturePair
{
    std::int32_t left;
    std::uint16_t right;
};

struct FixtureArray
{
    std::uint16_t values[3];
    std::int32_t tail;
};

struct FixtureNested
{
    FixturePair pair;
    std::int32_t extra;
};

union FixtureUnion
{
    std::int32_t integer;
    float decimal;
};

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_add_i32(std::int32_t left, std::int32_t right)
{
    return left + right;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_call_dependency(std::int32_t value)
{
    return ffi_fixture_dependency_scale(value);
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_pair_sum(const FixturePair* pair)
{
    return pair == nullptr ? 0 : pair->left + pair->right;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_pair_sum_value(FixturePair pair)
{
    return pair.left + pair.right;
}

FFI_FIXTURE_EXPORT FixturePair ffi_fixture_pair_make(std::int32_t left, std::uint16_t right)
{
    return { left, right };
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_array_sum(FixtureArray value)
{
    return value.values[0] + value.values[1] + value.values[2] + value.tail;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_nested_sum(FixtureNested value)
{
    return value.pair.left + value.pair.right + value.extra;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_union_integer(const FixtureUnion* value)
{
    return value == nullptr ? 0 : value->integer;
}

FFI_FIXTURE_EXPORT const char* ffi_fixture_greeting(void)
{
    return "ffi fixture";
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_invoke_callback(
    std::int32_t (*callback)(std::int32_t), std::int32_t value)
{
    return callback == nullptr ? 0 : callback(value);
}

FFI_FIXTURE_EXPORT double ffi_fixture_invoke_double_callback(
    double (*callback)(double, double), double left, double right)
{
    return callback == nullptr ? 0.0 : callback(left, right);
}

FFI_FIXTURE_EXPORT const char* ffi_fixture_invoke_string_callback(
    const char* (*callback)(const char*), const char* value)
{
    return callback == nullptr ? nullptr : callback(value);
}

FFI_FIXTURE_EXPORT std::int8_t ffi_fixture_negate_i8(std::int8_t value)
{
    return static_cast<std::int8_t>(-value);
}

FFI_FIXTURE_EXPORT std::uint8_t ffi_fixture_increment_u8(std::uint8_t value)
{
    return static_cast<std::uint8_t>(value + 1);
}

FFI_FIXTURE_EXPORT std::int16_t ffi_fixture_negate_i16(std::int16_t value)
{
    return static_cast<std::int16_t>(-value);
}

FFI_FIXTURE_EXPORT std::uint16_t ffi_fixture_increment_u16(std::uint16_t value)
{
    return static_cast<std::uint16_t>(value + 1);
}

FFI_FIXTURE_EXPORT bool ffi_fixture_bool_not(bool value)
{
    return !value;
}

FFI_FIXTURE_EXPORT double ffi_fixture_scale_f64(double value, double factor)
{
    return value * factor;
}

FFI_FIXTURE_EXPORT void* ffi_fixture_null(void)
{
    return nullptr;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_is_null(const void* pointer)
{
    return pointer == nullptr ? 1 : 0;
}

FFI_FIXTURE_EXPORT std::uint32_t ffi_fixture_sum_bytes(const std::uint8_t* data, std::uint32_t count)
{
    if (data == nullptr) return 0;

    std::uint32_t sum = 0;
    for (std::uint32_t i = 0; i < count; ++i)
        sum += data[i];
    return sum;
}

namespace
{
std::atomic<std::int32_t> g_fixtureAllocations{0};
}

FFI_FIXTURE_EXPORT void* ffi_fixture_allocate(std::uint32_t size)
{
    auto* memory = new std::uint8_t[size == 0 ? 1 : size];
    ++g_fixtureAllocations;
    return memory;
}

FFI_FIXTURE_EXPORT void ffi_fixture_release(void* pointer)
{
    if (pointer == nullptr) return;
    delete[] static_cast<std::uint8_t*>(pointer);
    --g_fixtureAllocations;
}

FFI_FIXTURE_EXPORT std::int32_t ffi_fixture_allocation_count(void)
{
    return g_fixtureAllocations.load();
}


FFI_FIXTURE_EXPORT std::uint64_t ffi_fixture_large_u64(void)
{
    return (UINT64_C(1) << 53) + UINT64_C(1);
}

FFI_FIXTURE_EXPORT std::int64_t ffi_fixture_large_i64(void)
{
    return -static_cast<std::int64_t>((UINT64_C(1) << 53) + UINT64_C(1));
}

FFI_FIXTURE_EXPORT std::uint64_t ffi_fixture_identity_u64(std::uint64_t value)
{
    return value;
}

FFI_FIXTURE_EXPORT void ffi_fixture_set_last_error(std::int32_t code)
{
#if defined(_WIN32)
    SetLastError(static_cast<DWORD>(code));
#else
    errno = code;
#endif
}

LODE_MODULE(vm)
{
    // This library is loaded through ffi.load in the test below.  Exporting a
    // valid Lode entrypoint still makes it usable by native-module tooling.
    return Lode::ModuleReturn(vm.CreateTable());
}
