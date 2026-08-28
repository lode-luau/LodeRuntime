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

project(LodeRuntime VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LODE_ABI_ID "lode-abi-1" CACHE STRING "ABI identifier exported by Lode modules")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Synchronize MSVC C++ Runtime DLLs (/MDd for Debug, /MD for Release across all dependencies)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeModule.cmake")

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
    if(APPLE)
        add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip>)
    else()
        add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
    endif()
endif()

include(FetchContent)

# These dependencies are provided by vcpkg in manifest mode. Luau and libuv
# remain FetchContent dependencies because Lode exports their build targets as
# part of the installed Lode distribution; libffi has its own module-specific FetchContent
# integration and must remain there as well.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

if(TARGET RapidJSON::RapidJSON)
    set(LODE_RAPIDJSON_TARGET RapidJSON::RapidJSON)
elseif(TARGET rapidjson)
    set(LODE_RAPIDJSON_TARGET rapidjson)
else()
    message(FATAL_ERROR "The vcpkg RapidJSON package did not provide RapidJSON::RapidJSON or rapidjson")
endif()

if(TARGET miniz::miniz)
    set(LODE_MINIZ_TARGET miniz::miniz)
elseif(TARGET miniz::miniz_static)
    set(LODE_MINIZ_TARGET miniz::miniz_static)
else()
    message(FATAL_ERROR "The vcpkg miniz package did not provide miniz::miniz or miniz::miniz_static")
endif()

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
# tree. Prevent their own install scripts from being pulled into the Lode distribution
# package; Lode exports the targets and headers it intentionally supports
# below.
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(luau libuv)
unset(INSTALL_PROJECT CACHE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    src/PackageArchive.cpp
    src/PackagePacker.cpp
    src/HttpDownloader.cpp
    src/PackageArtifact.cpp
    src/PackageManifest.cpp
    src/PackageValidator.cpp
    src/PackageGraph.cpp
    src/PackageLockfile.cpp
    src/PackageCache.cpp
    src/PackageConfig.cpp
    src/PackageInstaller.cpp
    src/GitResolver.cpp
    src/CiGenerator.cpp
    src/ProjectInitializer.cpp
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
    src/PackageConfig.hpp
    src/PackageInstaller.hpp
    src/PackageArchive.hpp
    src/PackagePacker.hpp
    src/HttpDownloader.hpp
    src/PackageArtifact.hpp
    src/PackageManifest.hpp
    src/GitResolver.hpp
    src/CiGenerator.hpp
    src/ProjectInitializer.hpp
    src/Platform/Platform.hpp
    src/Platform/CrashHandler.hpp
    src/Platform/WindowsCrashHandler.hpp
    src/PathUtil.hpp
    src/Sha256.hpp
)

# LodeCore SHARED Library Target (Shared DLL linked dynamically by runtime and native modules)
add_library(LodeCore SHARED
    ${LODE_CORE_SOURCES}
    ${LODE_CORE_HEADERS}
)
target_compile_definitions(LodeCore PRIVATE LODE_CORE_BUILD MINIZ_STATIC_DEFINE)
target_link_libraries(LodeCore PRIVATE
    nlohmann_json::nlohmann_json
    ${LODE_RAPIDJSON_TARGET}
    ${LODE_MINIZ_TARGET}
)
if(WIN32)
    target_link_libraries(LodeCore PRIVATE winhttp)
endif()

# Compile each target (runtime + every native module) with its own build
# configuration name. The loader compares a module's LodeModuleConfig export
# against the running runtime's config before calling LodeModuleInit, refusing
# to load a module built with a different CRT/STL ABI (Debug vs Release) than
# the runtime.
target_compile_definitions(LodeCore PUBLIC LODE_BUILD_CONFIG_NAME=$<CONFIG> LODE_HAS_BUILD_CONFIG)
target_compile_definitions(LodeCore PUBLIC LODE_ABI_ID=\"${LODE_ABI_ID}\")

# Public Lode target names. LodeCore remains the implementation target used by
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

target_link_libraries(lode PRIVATE
    LodeCore
    nlohmann_json::nlohmann_json
    Luau.Compiler
    Luau.Ast
    Luau.Analysis
)

# Installed runtime binaries and their vcpkg shared dependencies are kept in
# the same bin directory by the packaging script. Make that layout relocatable
# on POSIX systems; Windows searches the executable directory by default.
if(APPLE)
    set(LODE_RUNTIME_RPATH "@loader_path")
elseif(UNIX)
    set(LODE_RUNTIME_RPATH "$ORIGIN")
endif()
if(DEFINED LODE_RUNTIME_RPATH)
    set_target_properties(LodeCore lode PROPERTIES
        INSTALL_RPATH "${LODE_RUNTIME_RPATH}"
    )
endif()

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
    COMPONENT Lode
)

foreach(LODE_LUAU_INCLUDE_DIR IN LISTS LODE_LUAU_INCLUDE_DIRS)
    install(DIRECTORY "${LODE_LUAU_INCLUDE_DIR}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Lode
    )
endforeach()

install(TARGETS LodeCore LodeModule ${LODE_LUAU_TARGETS} uv
    EXPORT LodeTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT Lode
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT Lode
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT Lode
)

install(TARGETS lode
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT Lode
    BUNDLE DESTINATION . COMPONENT Lode
)

set(LODE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Lode")

lode_get_platform_name(LODE_PLATFORM)
lode_get_architecture_name(LODE_ARCHITECTURE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lode-install.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/lode-install.json"
    @ONLY
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfig.cmake"
    INSTALL_DESTINATION "${LODE_INSTALL_CMAKEDIR}"
)

# LodeConfigVersion stays at PROJECT_VERSION (1.0.0) even for nightly
# builds (1.0.0-nightly.YYYYMMDD.N). The nightly tag is prerelease per SemVer
# and CMake's version comparison treats 1.0.0-nightly < 1.0.0, so exposing the
# nightly string here would break find_package(Lode 1.0.0). The exact nightly
# is authoritative in the Lode VERSION file and lode-install.json (see
# nightly.yml / cmake/lode-install.json.in), while the CMake package version
# remains 1.0.0 with SameMajorVersion compatibility.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(EXPORT LodeTargets
    FILE LodeTargets.cmake
    NAMESPACE Lode::
    DESTINATION "${LODE_INSTALL_CMAKEDIR}"
    COMPONENT Lode
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lode-install.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/lode"
    COMPONENT Lode
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LodeConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LodeModule.cmake"
    DESTINATION "${LODE_INSTALL_CMAKEDIR}"
    COMPONENT Lode
)
