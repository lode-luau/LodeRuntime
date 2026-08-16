cmake_minimum_required(VERSION 3.14)

project(simd_module)

# Add AVX2 support flags
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
endif()

add_library(simd_module MODULE src/simd_module.cpp)

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

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_ARCH "x64")
else()
    set(TARGET_ARCH "x86")
endif()

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
