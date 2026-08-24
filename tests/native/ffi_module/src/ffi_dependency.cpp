// Dependency of the deterministic FFI fixture. Kept separate so the test
// exercises loading a library whose adjacent dependency must also resolve.
#include <cstdint>

#if defined(_WIN32)
#define FFI_DEPENDENCY_EXPORT extern "C" __declspec(dllexport)
#else
#define FFI_DEPENDENCY_EXPORT extern "C" __attribute__((visibility("default")))
#endif

FFI_DEPENDENCY_EXPORT std::int32_t ffi_fixture_dependency_scale(std::int32_t value)
{
    return value * 7;
}
