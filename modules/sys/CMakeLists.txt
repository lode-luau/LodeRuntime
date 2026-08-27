cmake_minimum_required(VERSION 3.20)
project(sys LANGUAGES CXX)

lode_add_module(sys
    SOURCES
        src/Main.cpp
        src/SysInfo.cpp
        src/SysProcess.cpp
        src/SysSignals.cpp
)

target_include_directories(sys PUBLIC include)
target_compile_definitions(sys PRIVATE LODE_SYS_BUILD NOMINMAX)

if(MSVC)
    target_compile_options(sys PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(sys PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(sys PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(sys PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
