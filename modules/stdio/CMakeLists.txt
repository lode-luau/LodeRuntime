cmake_minimum_required(VERSION 3.20)
project(stdio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(stdio SHARED
    src/Main.cpp
    src/StdioManager.cpp
    src/StdioStream.cpp
    src/StdioHelpers.cpp
)

target_include_directories(stdio PUBLIC include)
target_compile_definitions(stdio PRIVATE LODE_STDIO_BUILD NOMINMAX)
target_link_libraries(stdio PRIVATE LodeCore)

if(MSVC)
    target_compile_options(stdio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(stdio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(stdio PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(stdio PRIVATE -flto -Wl,--gc-sections)
endif()

if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "stdio.dll")
elseif(EMSCRIPTEN)
    set(TARGET_PLATFORM "wasm")
    set(TARGET_LIB_NAME "stdio.wasm")
elseif(ANDROID)
    set(TARGET_PLATFORM "android")
    set(TARGET_LIB_NAME "stdio.so")
elseif(IOS)
    set(TARGET_PLATFORM "ios")
    set(TARGET_LIB_NAME "stdio.dylib")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    set(TARGET_PLATFORM "freebsd")
    set(TARGET_LIB_NAME "stdio.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
    set(TARGET_PLATFORM "solaris")
    set(TARGET_LIB_NAME "stdio.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
    set(TARGET_PLATFORM "haiku")
    set(TARGET_LIB_NAME "stdio.so")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "stdio.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "stdio.so")
endif()

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

set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/$<CONFIG>")

foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_NAME} CONFIG_NAME_UPPER)
    set_target_properties(stdio PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
    )
endforeach()

if(NOT CMAKE_CONFIGURATION_TYPES)
    set_target_properties(stdio PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NATIVE_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${NATIVE_OUTPUT_DIR}"
    )
endif()
