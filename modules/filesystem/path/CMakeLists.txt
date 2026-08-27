cmake_minimum_required(VERSION 3.20)
project(path LANGUAGES CXX)

lode_add_module(path
    SOURCES
        src/Main.cpp
        src/Path.cpp
)

target_compile_definitions(path PRIVATE LODE_MODULE_BUILD)
target_include_directories(path PRIVATE src include)

if(MSVC)
    target_compile_options(path PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(path PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(path PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(path PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
