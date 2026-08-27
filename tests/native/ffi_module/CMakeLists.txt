cmake_minimum_required(VERSION 3.20)
project(ffi_module LANGUAGES CXX)

# A small, self-contained shared library used to exercise the ffi module in
# CTest.  Do not replace this with libc/libm: minimal Linux images such as
# Alpine use musl and do not provide libc.so.6.
lode_add_module(ffi_module
    SOURCES src/ffi_module.cpp
)

# Keep a real dependent shared library beside the fixture. This lets the
# integration script exercise ffi.load(..., { searchDllDirectory = true })
# without relying on a system DLL or process-wide loader configuration.
add_library(ffi_dependency SHARED src/ffi_dependency.cpp)
target_compile_features(ffi_dependency PRIVATE cxx_std_20)
set_target_properties(ffi_dependency PROPERTIES PREFIX "")

lode_get_platform_name(ffi_fixture_platform)
lode_get_architecture_name(ffi_fixture_architecture)
foreach(ffi_fixture_config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${ffi_fixture_config}" ffi_fixture_config_upper)
    set_target_properties(ffi_dependency PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${ffi_fixture_config_upper}
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ffi_fixture_platform}/${ffi_fixture_architecture}/${ffi_fixture_config}"
        LIBRARY_OUTPUT_DIRECTORY_${ffi_fixture_config_upper}
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ffi_fixture_platform}/${ffi_fixture_architecture}/${ffi_fixture_config}"
    )
endforeach()

if(NOT CMAKE_CONFIGURATION_TYPES)
    if(CMAKE_BUILD_TYPE)
        set(ffi_fixture_single_config_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ffi_fixture_platform}/${ffi_fixture_architecture}/${CMAKE_BUILD_TYPE}")
    else()
        set(ffi_fixture_single_config_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ffi_fixture_platform}/${ffi_fixture_architecture}")
    endif()
    set_target_properties(ffi_dependency PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ffi_fixture_single_config_directory}"
        LIBRARY_OUTPUT_DIRECTORY "${ffi_fixture_single_config_directory}"
    )
endif()

target_link_libraries(ffi_module PRIVATE ffi_dependency)
if(UNIX AND NOT APPLE)
    # The production loader receives an absolute fixture path; make its local
    # dependency relationship explicit for ELF platforms as well.
    set_target_properties(ffi_module PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
