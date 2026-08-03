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

set_target_properties(simd_module PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

# Output it into the temp directory alongside native_module so tests can find it
set_target_properties(simd_module PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/bin/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/bin/$<CONFIG>"
)

# Also copy to libs/windows/x64 so the standard path resolution can find it
add_custom_command(TARGET simd_module POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:simd_module> "${CMAKE_CURRENT_SOURCE_DIR}/libs/windows/x64/$<TARGET_FILE_NAME:simd_module>"
    COMMENT "Copying simd_module binary to libs/windows/x64"
)
