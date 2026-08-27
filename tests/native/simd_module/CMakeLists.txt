cmake_minimum_required(VERSION 3.14)

project(simd_module)

add_library(simd_module MODULE src/simd_module.cpp)

# AVX2 is available on x86/x64, but not on Apple Silicon. The fixture keeps
# its scalar implementation available on other hosts so the native-loader
# regression remains portable.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|x86|X86")
    if(MSVC)
        target_compile_options(simd_module PRIVATE /arch:AVX2)
    else()
        target_compile_options(simd_module PRIVATE -mavx2)
    endif()
    target_compile_definitions(simd_module PRIVATE LODE_SIMD_AVX2)
endif()

# Link against LodeCore which provides the Luau headers and Lode::* API
target_link_libraries(simd_module PRIVATE LodeCore)

if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(_simd_module_suffix ".dll")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(_simd_module_suffix ".dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(_simd_module_suffix ".so")
endif()

lode_get_architecture_name(TARGET_ARCH)

set_target_properties(simd_module PROPERTIES
    PREFIX ""
    SUFFIX "${_simd_module_suffix}"
)

# Output the simd_module binary directly into its own libs/<platform>/<arch>/<config>
# directory, so it never lands next to the runtime in build/bin. Per-config
# properties override the top-level per-config output directories from the
# root CMakeLists.
foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_NAME} CONFIG_NAME_UPPER)
    set_target_properties(simd_module PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
    )
endforeach()

if(NOT CMAKE_CONFIGURATION_TYPES)
    set_target_properties(simd_module PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}"
    )
endif()
