cmake_minimum_required(VERSION 3.20)
project(address LANGUAGES CXX)

lode_add_module(address SOURCES src/Main.cpp)
target_include_directories(address PRIVATE ../ffi/src)
