cmake_minimum_required(VERSION 3.20)
project(marshal_module LANGUAGES CXX)

lode_add_module(marshal_module
    SOURCES
        src/marshal_module.cpp
)

if(MSVC)
    target_compile_options(marshal_module PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2> /GR- /GL)
    target_link_options(marshal_module PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(marshal_module PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(marshal_module PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
