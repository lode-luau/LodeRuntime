cmake_minimum_required(VERSION 3.20)
project(json LANGUAGES CXX)

lode_add_module(json
    SOURCES
        src/json.cpp
)

if(MSVC)
    target_compile_options(json PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(json PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(json PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(json PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
