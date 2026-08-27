cmake_minimum_required(VERSION 3.20)
project(ffi LANGUAGES CXX)

# Backend: libffi. The ffi_cif for every bound
# prototype is prepared a single time at bind time (see src/Main.cpp), so the
# per-call cost is one cached-plan ffi_call -- no layout re-derivation.
include(FetchContent)

if(NOT TARGET ffi_static)
    set(LIBFFI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBFFI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libffi
        GIT_REPOSITORY https://github.com/python-cmake-buildsystem/libffi.git
        GIT_TAG        libffi-cmake-buildsystem-v3.5.2-2025-12-26-2263d60
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(libffi)
    if(NOT libffi_POPULATED)
        FetchContent_Populate(libffi)
    endif()

    # The fork's MSVC MASM preprocessing command omits the generated include
    # directory. win64_intel.S includes fficonfig.h directly.
    if(MSVC)
        file(READ "${libffi_SOURCE_DIR}/configure_options.cmake" LIBFFI_CONFIGURE_OPTIONS)
        string(FIND
            "${LIBFFI_CONFIGURE_OPTIONS}"
            "/I\"${CMAKE_BINARY_DIR}/include\""
            LIBFFI_GENERATED_INCLUDE_INDEX
        )
        if(LIBFFI_GENERATED_INCLUDE_INDEX EQUAL -1)
            string(REPLACE
                "/Iinclude"
                "/I\"${CMAKE_BINARY_DIR}/include\" /Iinclude"
                LIBFFI_CONFIGURE_OPTIONS
                "${LIBFFI_CONFIGURE_OPTIONS}"
            )
            file(WRITE "${libffi_SOURCE_DIR}/configure_options.cmake" "${LIBFFI_CONFIGURE_OPTIONS}")
        endif()
    endif()

    if(NOT TARGET ffi_static)
        add_subdirectory("${libffi_SOURCE_DIR}" "${libffi_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
endif()

if(NOT TARGET ffi_static)
    message(FATAL_ERROR "The CMake libffi backend did not create the ffi_static target")
endif()

# vcpkg's MSBuild app-local step is only meaningful for executables and DLLs.
# libffi also exposes its intermediate OBJECT/static-library targets to the
# Visual Studio generator, where that step otherwise tries to inspect .lib
# files as executable binaries.
foreach(LODE_FFI_TARGET IN ITEMS objlib ffi_static ffi_shared)
    if(TARGET ${LODE_FFI_TARGET})
        set_target_properties(${LODE_FFI_TARGET} PROPERTIES
            VS_GLOBAL_VcpkgApplocalDeps "false"
        )
    endif()
endforeach()

set(LODE_FFI_LIB_TARGET ffi_static)

lode_add_module(ffi
    SOURCES
        src/Main.cpp
        src/CdefParser.cpp
        src/DynamicLibrary.cpp
)

target_include_directories(ffi PRIVATE include src)
if(DEFINED libffi_BINARY_DIR)
    target_include_directories(ffi PRIVATE
        "${CMAKE_BINARY_DIR}/include"
        "${libffi_BINARY_DIR}/include"
        "${libffi_SOURCE_DIR}/include"
    )
endif()
target_compile_definitions(ffi PRIVATE
    LODE_FFI_BUILD
    FFI_STATIC_BUILD
    NOMINMAX
)
target_link_libraries(ffi PRIVATE ${LODE_FFI_LIB_TARGET})

if(NOT WIN32 AND NOT APPLE)
    # dlopen/dlsym/dlclose live in libdl on Linux.
    target_link_libraries(ffi PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(ffi PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(ffi PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(ffi PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(ffi PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
