cmake_minimum_required(VERSION 3.20)
project(stdio LANGUAGES CXX)

lode_add_module(stdio
    SOURCES
        src/Main.cpp
        src/StdioManager.cpp
        src/StdioStream.cpp
        src/StdioHelpers.cpp
)

target_include_directories(stdio PUBLIC include)
target_compile_definitions(stdio PRIVATE LODE_STDIO_BUILD NOMINMAX)

if(MSVC)
    target_compile_options(stdio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(stdio PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(stdio PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(stdio PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
