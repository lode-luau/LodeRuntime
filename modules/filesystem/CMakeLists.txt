cmake_minimum_required(VERSION 3.20)
project(filesystem LANGUAGES CXX)

lode_add_module(filesystem
    SOURCES
        src/Main.cpp
        src/FileHandle.cpp
        src/ReadStream.cpp
        src/FileWatcher.cpp
        src/FsStatic.cpp
        src/FsClasses.cpp
)

target_include_directories(filesystem PRIVATE include path/include)

add_subdirectory(path)

if(MSVC)
    target_compile_options(filesystem PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(filesystem PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(filesystem PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(filesystem PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
