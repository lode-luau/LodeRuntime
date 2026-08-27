#include <Lode/Module.hpp>
#if defined(LODE_SIMD_AVX2)
#include <immintrin.h>
#endif
#include <iostream>

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    // C++ SIMD function to scale an array of floats
    // The Luau `buffer` ensures memory is contiguous.
    exports.Set("scale_floats", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsBuffer() || !args[1].IsNumber())
        {
            return Lode::Value(false);
        }

        size_t bufferSize = 0;
        void* bufferPtr = args[0].AsBuffer(&bufferSize);
        float scalar = static_cast<float>(args[1].AsNumber());

        if (!bufferPtr || bufferSize == 0)
        {
            return Lode::Value(false);
        }

        // We assume the buffer contains floats. Size in bytes / 4 = number of floats.
        size_t count = bufferSize / sizeof(float);
        float* data = static_cast<float*>(bufferPtr);

        size_t i = 0;
#if defined(LODE_SIMD_AVX2)
        // Process 8 floats at a time using AVX2 on x86/x64 hosts.
        __m256 v_scalar = _mm256_set1_ps(scalar);
        
        for (; i + 7 < count; i += 8)
        {
            // Load 8 floats
            __m256 v_data = _mm256_loadu_ps(&data[i]);
            // Multiply
            __m256 v_result = _mm256_mul_ps(v_data, v_scalar);
            // Store back
            _mm256_storeu_ps(&data[i], v_result);
        }
#endif

        // Process all values normally on non-AVX2 hosts and any remainder on
        // x86/x64 hosts.
        for (; i < count; ++i)
        {
            data[i] *= scalar;
        }

        return Lode::Value(true);
    }));

    return { Lode::Value(exports) };
}
