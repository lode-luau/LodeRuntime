cmake_minimum_required(VERSION 3.20)
project(native_module LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_module SHARED src/native_module.cpp)

target_link_libraries(native_module PRIVATE LodeCore)

# 1. Platform Detection
if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "native_module.dll")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "native_module.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "native_module.so")
endif()

# 2. Architecture Detection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64|AARCH64")
        set(TARGET_ARCH "arm64")
    else()
        set(TARGET_ARCH "x64")
    endif()
else()
    set(TARGET_ARCH "x86")
endif()

# 3. Dynamic Output Directory
set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}")

add_custom_command(TARGET native_module POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_module> "${NATIVE_OUTPUT_DIR}/${TARGET_LIB_NAME}"
    COMMENT "Copying native binary to libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${TARGET_LIB_NAME}"
)
