cmake_minimum_required(VERSION 3.20)
project(pipe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(pipe SHARED
    src/Main.cpp
    src/PipeManager.cpp
    src/PipeStream.cpp
    src/PipeServer.cpp
    src/PipeHelpers.cpp
)

target_include_directories(pipe PUBLIC include)
target_compile_definitions(pipe PRIVATE LODE_PIPE_BUILD NOMINMAX)
target_link_libraries(pipe PRIVATE LodeCore)

if(MSVC)
    target_compile_options(pipe PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(pipe PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(pipe PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(pipe PRIVATE -flto -Wl,--gc-sections)
endif()

if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "pipe.dll")
elseif(EMSCRIPTEN)
    set(TARGET_PLATFORM "wasm")
    set(TARGET_LIB_NAME "pipe.wasm")
elseif(ANDROID)
    set(TARGET_PLATFORM "android")
    set(TARGET_LIB_NAME "pipe.so")
elseif(IOS)
    set(TARGET_PLATFORM "ios")
    set(TARGET_LIB_NAME "pipe.dylib")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    set(TARGET_PLATFORM "freebsd")
    set(TARGET_LIB_NAME "pipe.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
    set(TARGET_PLATFORM "solaris")
    set(TARGET_LIB_NAME "pipe.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
    set(TARGET_PLATFORM "haiku")
    set(TARGET_LIB_NAME "pipe.so")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "pipe.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "pipe.so")
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
    set_target_properties(pipe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
    )
endforeach()
