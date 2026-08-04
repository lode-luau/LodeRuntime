cmake_minimum_required(VERSION 3.20)
project(filesystem LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(filesystem SHARED src/filesystem.cpp)

target_link_libraries(filesystem PRIVATE LodeCore)

# Size Optimization Flags (Option C)
if(MSVC)
    target_compile_options(filesystem PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(filesystem PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(filesystem PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(filesystem PRIVATE -flto -Wl,--gc-sections)
endif()

# 1. Universal Platform Detection
if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "filesystem.dll")
elseif(EMSCRIPTEN)
    set(TARGET_PLATFORM "wasm")
    set(TARGET_LIB_NAME "filesystem.wasm")
elseif(ANDROID)
    set(TARGET_PLATFORM "android")
    set(TARGET_LIB_NAME "filesystem.so")
elseif(IOS)
    set(TARGET_PLATFORM "ios")
    set(TARGET_LIB_NAME "filesystem.dylib")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    set(TARGET_PLATFORM "freebsd")
    set(TARGET_LIB_NAME "filesystem.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
    set(TARGET_PLATFORM "solaris")
    set(TARGET_LIB_NAME "filesystem.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
    set(TARGET_PLATFORM "haiku")
    set(TARGET_LIB_NAME "filesystem.so")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "filesystem.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "filesystem.so")
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
set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}")

add_custom_command(TARGET filesystem POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:filesystem> "${NATIVE_OUTPUT_DIR}/${TARGET_LIB_NAME}"
    COMMENT "Copying native binary to libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${TARGET_LIB_NAME}"
)
