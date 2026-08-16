cmake_minimum_required(VERSION 3.20)
project(sys LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(sys SHARED
    src/Main.cpp
    src/SysInfo.cpp
    src/SysProcess.cpp
)

target_include_directories(sys PUBLIC include)
target_compile_definitions(sys PRIVATE LODE_SYS_BUILD NOMINMAX)
target_link_libraries(sys PRIVATE LodeCore)

if(MSVC)
    target_compile_options(sys PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(sys PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(sys PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(sys PRIVATE -flto -Wl,--gc-sections)
endif()

if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "sys.dll")
elseif(EMSCRIPTEN)
    set(TARGET_PLATFORM "wasm")
    set(TARGET_LIB_NAME "sys.wasm")
elseif(ANDROID)
    set(TARGET_PLATFORM "android")
    set(TARGET_LIB_NAME "sys.so")
elseif(IOS)
    set(TARGET_PLATFORM "ios")
    set(TARGET_LIB_NAME "sys.dylib")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    set(TARGET_PLATFORM "freebsd")
    set(TARGET_LIB_NAME "sys.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
    set(TARGET_PLATFORM "solaris")
    set(TARGET_LIB_NAME "sys.so")
elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
    set(TARGET_PLATFORM "haiku")
    set(TARGET_LIB_NAME "sys.so")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "sys.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "sys.so")
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

add_custom_command(TARGET sys POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sys> "${NATIVE_OUTPUT_DIR}/${TARGET_LIB_NAME}"
    COMMENT "Copying native binary to libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${TARGET_LIB_NAME}"
)
