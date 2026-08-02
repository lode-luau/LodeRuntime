cmake_minimum_required(VERSION 3.20)
project(native_module LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_module SHARED src/native_module.cpp)

target_link_libraries(native_module PRIVATE LodeCore)

# Post-build command copying DLL to libs/windows/x64/
set(NATIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/windows/x64")

add_custom_command(TARGET native_module POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NATIVE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:native_module> "${NATIVE_OUTPUT_DIR}/native_module.dll"
    COMMENT "Copying native_module.dll to libs/windows/x64/"
)
