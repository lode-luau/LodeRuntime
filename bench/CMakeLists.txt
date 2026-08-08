cmake_minimum_required(VERSION 3.20)
project(lode_bench LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT LODE_BUILD_BENCH)
    return()
endif()

add_executable(lode_bench cpp/Bench.cpp)
target_link_libraries(lode_bench PRIVATE LodeCore)
