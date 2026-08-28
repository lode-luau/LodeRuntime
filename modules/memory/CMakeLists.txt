cmake_minimum_required(VERSION 3.20)
project(memory LANGUAGES CXX)

lode_add_module(memory SOURCES src/Main.cpp)
target_include_directories(memory PRIVATE ../ffi/src)
