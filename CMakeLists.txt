cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(LodeRuntime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Disable unnecessary Luau test targets to speed up build and save compiler memory
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Fetch Luau
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Fetch nlohmann_json for lightweight lode.json parsing
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
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

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${json_SOURCE_DIR}/include
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
    src/Error.cpp
    src/Value.cpp
    src/Table.cpp
    src/Coroutine.cpp
    src/Metatable.cpp
    src/EventLoop.cpp
    src/Task.cpp
    src/Module.cpp
    src/Registry.cpp
    src/ModuleLoader.cpp
    src/State.cpp
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
    include/Lode/Table.hpp
    include/Lode/Metatable.hpp
    include/Lode/ObjectWrap.hpp
    include/Lode/ClassBuilder.hpp
    include/Lode/EventLoop.hpp
    include/Lode/Task.hpp
    include/Lode/Coroutine.hpp
    include/Lode/Module.hpp
    include/Lode/State.hpp
    src/Platform/Platform.hpp
    src/Platform/CrashHandler.hpp
    src/Platform/WindowsCrashHandler.hpp
)

# LodeCore SHARED Library Target (Shared DLL linked dynamically by runtime and native modules)
add_library(LodeCore SHARED ${LODE_CORE_SOURCES} ${LODE_CORE_HEADERS})
target_compile_definitions(LodeCore PRIVATE LODE_CORE_BUILD)

target_include_directories(LodeCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(LodeCore PRIVATE
    Luau.VM
    Luau.Compiler
    Luau.Ast
    Luau.Require
    uv_a
)

# Main Executable LodeRuntime
add_executable(lode_runtime
    src/Runtime.cpp
)

target_link_libraries(lode_runtime PRIVATE LodeCore Luau.Compiler Luau.Ast)

# Add subdirectories for optional/test modules
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/temp/native_module/CMakeLists.txt")
    add_subdirectory(temp/native_module)
endif()
