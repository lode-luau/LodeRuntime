cmake_minimum_required(VERSION 3.20)

# MSVC debug info must be Embedded (/Z7) so each object file is self-contained
# instead of sharing one .pdb. With /Zi, concurrent CL.EXE processes need /FS
# to coordinate; sccache silently strips /FS (sccache#1012), so parallel Ninja
# builds then fail with C1041. /Z7 keeps the .obj fully cacheable by sccache.
# CMP0141 + CMAKE_MSVC_DEBUG_INFORMATION_FORMAT must be set before project(),
# because MSVC compiler flags are initialized during enable_language().
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Fix MSVC Runtime Library mismatch (LNK2038) for CMake 3.15+
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# rapidjson v1.1.0 declares cmake_minimum_required(VERSION 2.8), which modern
# CMake refuses to configure. Floor the minimum supported policy version so
# its header-only target can be added via FetchContent_MakeAvailable below.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(LodeRuntime VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LODE_ABI_ID "lode-abi-1" CACHE STRING "Native ABI identifier exported by the Lode SDK")

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
    # /O2 is the standard speed optimization. /GL and /LTCG were removed
    # because whole-program optimization / link-time codegen defeats most of
    # the sccache benefit.
    add_compile_options(
        /FS
        $<$<CONFIG:Release>:/O2>
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
# rapidjson builds docs/examples/tests by default, so disable them and let
# FetchContent_MakeAvailable add the header-only RapidJSON target.
FetchContent_Declare(
    rapidjson
    URL https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz
    URL_HASH SHA256=bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_THIRDPARTY_GTEST OFF CACHE BOOL "" FORCE)

# Fetch libuv for high-performance OS Event Loop
FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.48.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# FetchContent projects are build dependencies, not part of Lode's install
# tree. Prevent their own install scripts from being pulled into the SDK
# package; Lode exports the targets and headers it intentionally supports
# below.
set(CMAKE_SKIP_INSTALL_RULES TRUE)
FetchContent_MakeAvailable(luau json libuv rapidjson)
unset(CMAKE_SKIP_INSTALL_RULES)

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
    src/PackageValidator.cpp
    src/PackageGraph.cpp
    src/PackageLockfile.cpp
    src/PackageCache.cpp
    src/CiGenerator.cpp
    src/State.cpp
    src/StateLifetime.cpp
    src/Compiler.cpp
    src/Sha256.cpp
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
    src/PackageValidator.hpp
    src/PackageGraph.hpp
    src/PackageLockfile.hpp
    src/PackageCache.hpp
    src/CiGenerator.hpp
    src/Platform/Platform.hpp
    src/Platform/CrashHandler.hpp
    src/Platform/WindowsCrashHandler.hpp
    src/PathUtil.hpp
    src/Sha256.hpp
)

# LodeCore SHARED Library Target (Shared DLL linked dynamically by runtime and native modules)
add_library(LodeCore SHARED ${LODE_CORE_SOURCES} ${LODE_CORE_HEADERS})
target_compile_definitions(LodeCore PRIVATE LODE_CORE_BUILD)

# Compile each target (runtime + every native module) with its own build
# configuration name. The loader compares a module's LodeModuleConfig export
# against the running runtime's config before calling LodeModuleInit, refusing
# to load a module built with a different CRT/STL ABI (Debug vs Release) than
# the runtime.
target_compile_definitions(LodeCore PUBLIC LODE_BUILD_CONFIG_NAME=$<CONFIG> LODE_HAS_BUILD_CONFIG)
target_compile_definitions(LodeCore PUBLIC LODE_ABI_ID=\"${LODE_ABI_ID}\")

# Public SDK target names. LodeCore remains the implementation target used by
# this superbuild; the namespaced targets are the stable package-facing API.
set_target_properties(LodeCore PROPERTIES EXPORT_NAME Core)
add_library(LodeModule INTERFACE)
set_target_properties(LodeModule PROPERTIES EXPORT_NAME Module)
target_link_libraries(LodeModule INTERFACE LodeCore)
target_compile_definitions(LodeModule INTERFACE
    LODE_BUILD_CONFIG_NAME=$<CONFIG>
    LODE_HAS_BUILD_CONFIG
    LODE_ABI_ID=\"${LODE_ABI_ID}\"
)
add_library(Lode::Core ALIAS LodeCore)
add_library(Lode::Module ALIAS LodeModule)

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

# Main Executable lode (lode on Linux/macOS, lode.exe on Windows)
add_executable(lode
    src/Runtime.cpp
)

if(WIN32)
    target_sources(lode PRIVATE src/Runtime.rc)
endif()

target_link_libraries(lode PRIVATE LodeCore Luau.Compiler Luau.Ast Luau.Analysis)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeNativeModule.cmake")

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

# Installable CMake package used to build native packages outside this
# repository. All transitive Luau targets are exported because native modules
# call Luau's C API in addition to calling Lode's C++ wrappers.
set(LODE_LUAU_TARGETS
    Luau.Common
    Luau.Ast
    Luau.Bytecode
    Luau.Compiler
    Luau.Config
    Luau.Analysis
    Luau.CodeGen
    Luau.VM
    Luau.Require
    Luau.VM.Internals
)

set(LODE_LUAU_INCLUDE_DIRS
    "${luau_SOURCE_DIR}/Common/include"
    "${luau_SOURCE_DIR}/Ast/include"
    "${luau_SOURCE_DIR}/Bytecode/include"
    "${luau_SOURCE_DIR}/Compiler/include"
    "${luau_SOURCE_DIR}/Config/include"
    "${luau_SOURCE_DIR}/Analysis/include"
    "${luau_SOURCE_DIR}/CodeGen/include"
    "${luau_SOURCE_DIR}/VM/include"
    "${luau_SOURCE_DIR}/Require/include"
)

set(LODE_LUAU_BUILD_INCLUDE_DIRS
    ${LODE_LUAU_INCLUDE_DIRS}
    "${luau_SOURCE_DIR}/VM/src"
)

# Luau's upstream targets use source-tree include paths. Replace those
# interface paths with relocatable build/install paths before exporting them.
foreach(LODE_LUAU_TARGET IN LISTS LODE_LUAU_TARGETS)
    if(TARGET "${LODE_LUAU_TARGET}")
        set_target_properties("${LODE_LUAU_TARGET}" PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES
                "$<BUILD_INTERFACE:${LODE_LUAU_BUILD_INCLUDE_DIRS}>;$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        )
    endif()
endforeach()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT LodeSDK
)

foreach(LODE_LUAU_INCLUDE_DIR IN LISTS LODE_LUAU_INCLUDE_DIRS)
    install(DIRECTORY "${LODE_LUAU_INCLUDE_DIR}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT LodeSDK
    )
endforeach()

install(TARGETS LodeCore LodeModule ${LODE_LUAU_TARGETS} uv
    EXPORT LodeTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT LodeSDK
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT LodeSDK
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT LodeSDK
)

install(TARGETS lode
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT LodeSDK
    BUNDLE DESTINATION . COMPONENT LodeSDK
)

set(LODE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Lode")

lode_get_platform_name(LODE_SDK_PLATFORM)
lode_get_architecture_name(LODE_SDK_ARCHITECTURE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lode-sdk.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/lode-sdk.json"
    @ONLY
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfig.cmake"
    INSTALL_DESTINATION "${LODE_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(EXPORT LodeTargets
    FILE LodeTargets.cmake
    NAMESPACE Lode::
    DESTINATION "${LODE_INSTALL_CMAKEDIR}"
    COMPONENT LodeSDK
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lode-sdk.json"
    DESTINATION .
    COMPONENT LodeSDK
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeNativeModule.cmake"
    DESTINATION "${LODE_INSTALL_CMAKEDIR}"
    COMPONENT LodeSDK
)
