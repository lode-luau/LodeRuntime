cmake_minimum_required(VERSION 3.20)
project(udp LANGUAGES CXX)

lode_add_native_module(udp
    SOURCES
        src/Main.cpp
        src/UdpSocket.cpp
        src/UdpHelpers.cpp
)

target_include_directories(udp PUBLIC include)
target_compile_definitions(udp PRIVATE LODE_UDP_BUILD NOMINMAX)

if(MSVC)
    target_compile_options(udp PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(udp PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(udp PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(udp PRIVATE -flto -Wl,--gc-sections)
endif()
