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
    set(_simd_module_suffix ".dll")
elseif(APPLE)
    set(_simd_module_suffix ".dylib")
else()
    set(_simd_module_suffix ".so")
endif()

set_target_properties(simd_module PROPERTIES
    PREFIX ""
    SUFFIX "${_simd_module_suffix}"
)

# Output it into the build output directory for the active configuration
set_target_properties(simd_module PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/bin/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build/bin/$<CONFIG>"
)

if(WIN32)
    add_custom_command(TARGET simd_module POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/libs/windows/x64/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:simd_module> "${CMAKE_CURRENT_SOURCE_DIR}/libs/windows/x64/$<CONFIG>/$<TARGET_FILE_NAME:simd_module>"
        COMMENT "Copying simd_module binary to libs/windows/x64/$<CONFIG>"
    )
elseif(APPLE)
    add_custom_command(TARGET simd_module POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/libs/macos/x64/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:simd_module> "${CMAKE_CURRENT_SOURCE_DIR}/libs/macos/x64/$<CONFIG>/$<TARGET_FILE_NAME:simd_module>"
        COMMENT "Copying simd_module binary to libs/macos/x64/$<CONFIG>"
    )
else()
    add_custom_command(TARGET simd_module POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/libs/linux/x64/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:simd_module> "${CMAKE_CURRENT_SOURCE_DIR}/libs/linux/x64/$<CONFIG>/$<TARGET_FILE_NAME:simd_module>"
        COMMENT "Copying simd_module binary to libs/linux/x64/$<CONFIG>"
    )
endif()
