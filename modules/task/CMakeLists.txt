# Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(task LANGUAGES CXX)

lode_add_native_module(task
    SOURCES
        src/task.cpp
)

# Size Optimization Flags (Option C)
if(MSVC)
    target_compile_options(task PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(task PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(task PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(task PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
