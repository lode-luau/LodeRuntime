cmake_minimum_required(VERSION 3.20)
project(config_mismatch_module LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(config_mismatch_module SHARED src/main.cpp)

target_link_libraries(config_mismatch_module PRIVATE LodeCore)

# Named like the other fixtures so tests require it via "./config_mismatch_module".
set_target_properties(config_mismatch_module PROPERTIES PREFIX "")

if(WIN32)
    set(TARGET_PLATFORM "windows")
    set(TARGET_LIB_NAME "config_mismatch_module.dll")
elseif(APPLE)
    set(TARGET_PLATFORM "macos")
    set(TARGET_LIB_NAME "config_mismatch_module.dylib")
else()
    set(TARGET_PLATFORM "linux")
    set(TARGET_LIB_NAME "config_mismatch_module.so")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_ARCH "x64")
else()
    set(TARGET_ARCH "x86")
endif()

set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/$<CONFIG>")

foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_NAME} CONFIG_NAME_UPPER)
    set_target_properties(config_mismatch_module PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_NAME_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${TARGET_PLATFORM}/${TARGET_ARCH}/${CONFIG_NAME}"
    )
endforeach()

if(NOT CMAKE_CONFIGURATION_TYPES)
    set_target_properties(config_mismatch_module PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${NATIVE_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${NATIVE_OUTPUT_DIR}"
    )
endif()
