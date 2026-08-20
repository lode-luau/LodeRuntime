cmake_minimum_required(VERSION 3.20)
project(abi_mismatch_module LANGUAGES CXX)

lode_add_native_module(abi_mismatch_module
    SOURCES src/main.cpp
)
