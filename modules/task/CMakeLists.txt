# Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(task LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(task SHARED src/task.cpp)

target_link_libraries(task PRIVATE LodeCore)

# Size Optimization Flags (Option C)
if(MSVC)
    target_compile_options(task PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(task PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(task PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(task PRIVATE -flto -Wl,--gc-sections)
endif()

# 1. Universal Platform Detection
if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "task.dll")
elseif(EMSCRIPTEN)
    set(TARGET_PLATFORM "wasm")
    set(TARGET_LIB_NAME "task.wasm")
elseif(ANDROID)
    set(TARGET_PLATFORM "android")
    set(TARGET_LIB_NAME "task.so")
elseif(IOS)
    set(TARGET_PLATFORM "ios")
    set(TARGET_LIB_NAME "task.dylib")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    set(TARGET_PLATFORM "freebsd")
    set(TARGET_LIB_NAME "task.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
    set(TARGET_PLATFORM "solaris")
    set(TARGET_LIB_NAME "task.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
    set(TARGET_PLATFORM "haiku")
    set(TARGET_LIB_NAME "task.so")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "task.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "task.so")
endif()

# 2. Universal Architecture Detection
if(EMSCRIPTEN)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TARGET_ARCH "wasm64")
    else()
        set(TARGET_ARCH "wasm32")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64|AARCH64")
        set(TARGET_ARCH "arm64")
    else()
        set(TARGET_ARCH "x64")
    endif()
else()
    set(TARGET_ARCH "x86")
endif()

# 3. Dynamic Output Directory
set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/$<CONFIG>")

foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_NAME} CONFIG_NAME_UPPER)
    set_target_properties(task PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
    )
endforeach()
