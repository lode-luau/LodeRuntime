cmake_minimum_required(VERSION 3.20)
project(tty LANGUAGES CXX)

lode_add_native_module(tty
    SOURCES
        src/Main.cpp
        src/TtyManager.cpp
        src/TtyStream.cpp
        src/TtyHelpers.cpp
)

target_include_directories(tty PUBLIC include)
target_compile_definitions(tty PRIVATE LODE_TTY_BUILD NOMINMAX)

if(MSVC)
    target_compile_options(tty PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(tty PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(tty PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(tty PRIVATE -flto -Wl,--gc-sections)
endif()
