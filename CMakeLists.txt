cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Fix MSVC Runtime Library mismatch (LNK2038) for CMake 3.15+
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(LodeRuntime LANGUAGES C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Synchronize MSVC C++ Runtime DLLs (/MDd for Debug, /MD for Release across all dependencies)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Cross-platform binary output directory configuration (Separated by Build Configuration)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# For Multi-Config generators like Visual Studio (MSVC)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}")
endforeach()

# For Single-Config generators like Ninja / Makefiles
if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}")
endif()

if(MSVC)
    # /O2 is the standard speed optimization. /Zi (not /ZI) keeps MSVC output
    # cacheable by sccache; /GL and /LTCG were removed because whole-program
    # optimization / link-time codegen defeats most of the sccache benefit.
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Zi>
    )
    add_link_options(
        # Keep dead symbol and duplicate-COMDAT removal
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    add_compile_options($<$<CONFIG:Release>:-Os> $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
endif()

include(FetchContent)

set(LUAU_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_CLI      OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_ANALYSIS OFF CACHE BOOL "" FORCE)

option(LODE_BUILD_BENCH "Build the lode_bench benchmark harness (bench/)" OFF)

FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG        0.732
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)


# Fetch nlohmann_json for lightweight lode.json parsing
# URL_HASH pins the exact artifact content (supply-chain / reproducibility
# guard): if upstream ever serves different bytes for this tag, configure
# fails loudly instead of silently building against unknown code.
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Fetch rapidjson for the native JSON parse/stringify core (SAX Reader +
# Writer; header-only, no intermediate DOM). See src/Json.cpp.
# NOTE: rapidjson's own CMakeLists.txt requires CMake < 3.5, so it cannot be
# added with MakeAvailable; header-only, so we only need its include dir.
FetchContent_Declare(
    rapidjson
    URL https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz
    URL_HASH SHA256=bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Fetch libuv for high-performance OS Event Loop
FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.48.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(luau json libuv)

FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${json_SOURCE_DIR}/include
    ${rapidjson_SOURCE_DIR}/include
    ${luau_SOURCE_DIR}/VM/include
    ${luau_SOURCE_DIR}/Compiler/include
    ${luau_SOURCE_DIR}/Ast/include
    ${luau_SOURCE_DIR}/Require/include
    ${luau_SOURCE_DIR}/Config/include
    ${luau_SOURCE_DIR}/CLI/include
    ${luau_SOURCE_DIR}/Common/include
    ${libuv_SOURCE_DIR}/include
)

set(LODE_CORE_SOURCES
    src/Value.cpp
    src/StackValue.cpp
    src/Vector.cpp
    src/Buffer.cpp
    src/Table.cpp
    src/Coroutine.cpp
    src/Signal.cpp
    src/Metatable.cpp
    src/EventLoop.cpp
    src/Task.cpp
    src/Module.cpp
    src/Registry.cpp
    src/ModuleLoader.cpp
    src/State.cpp
    src/StateLifetime.cpp
    src/Compiler.cpp
    src/Gc.cpp
    src/Logger.cpp
    src/Json.cpp
    src/TaskContext.hpp
    src/Platform/Platform.cpp
    src/Platform/WindowsDll.cpp
    src/Platform/UnixDlopen.cpp
    src/Platform/WindowsCrashHandler.cpp
    src/Platform/PosixCrashHandler.cpp
)

set(LODE_CORE_HEADERS
    include/Lode/Export.hpp
    include/Lode/Error.hpp
    include/Lode/Result.hpp
    include/Lode/Value.hpp
    include/Lode/Vector.hpp
    include/Lode/Buffer.hpp
    include/Lode/Table.hpp
    include/Lode/Metatable.hpp
    include/Lode/Signal.hpp
    include/Lode/ObjectWrap.hpp
    include/Lode/ClassBuilder.hpp
    include/Lode/EventLoop.hpp
    include/Lode/Task.hpp
    include/Lode/Coroutine.hpp
    include/Lode/Module.hpp
    include/Lode/StackValue.hpp
    include/Lode/State.hpp
    include/Lode/Compiler.hpp
    include/Lode/Gc.hpp
    include/Lode/Logger.hpp
    include/Lode/Json.hpp
    src/Platform/Platform.hpp
    src/Platform/CrashHandler.hpp
    src/Platform/WindowsCrashHandler.hpp
    src/PathUtil.hpp
)

# LodeCore SHARED Library Target (Shared DLL linked dynamically by runtime and native modules)
add_library(LodeCore SHARED ${LODE_CORE_SOURCES} ${LODE_CORE_HEADERS})
target_compile_definitions(LodeCore PRIVATE LODE_CORE_BUILD)

target_include_directories(LodeCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LodeCore PUBLIC
    Luau.VM
    Luau.Compiler
    Luau.Ast
    Luau.Analysis
    Luau.Require
    Luau.CodeGen
    Luau.VM.Internals
    uv
    ${CMAKE_DL_LIBS}
)

# Main Executable LodeRuntime (lode_runtime on Linux/macOS, lode_runtime.exe on Windows)
add_executable(lode_runtime
    src/Runtime.cpp
)

target_link_libraries(lode_runtime PRIVATE LodeCore Luau.Compiler Luau.Ast Luau.Analysis)

# Benchmark harness (opt-in: -DLODE_BUILD_BENCH=ON keeps the nightly build unchanged)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt")
    add_subdirectory(bench)
endif()

# All native modules under modules/ are aggregated by modules/CMakeLists.txt,
# which globs for CMakeLists.txt files so new modules need no top-level edit.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/CMakeLists.txt")
    add_subdirectory(modules)
endif()

# Regression/integration tests, driven by CTest. See tests/CMakeLists.txt.
enable_testing()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
