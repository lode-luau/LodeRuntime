cmake_minimum_required(VERSION 3.20)
project(pipe LANGUAGES CXX)

lode_add_native_module(pipe
    SOURCES
        src/Main.cpp
        src/PipeManager.cpp
        src/PipeStream.cpp
        src/PipeServer.cpp
        src/PipeHelpers.cpp
)

target_include_directories(pipe PUBLIC include)
target_compile_definitions(pipe PRIVATE LODE_PIPE_BUILD NOMINMAX)

if(MSVC)
    target_compile_options(pipe PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(pipe PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(pipe PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(pipe PRIVATE -flto -Wl,--gc-sections)
endif()
