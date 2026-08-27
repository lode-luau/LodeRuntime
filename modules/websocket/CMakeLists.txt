cmake_minimum_required(VERSION 3.20)
project(websocket LANGUAGES CXX)

lode_add_module(websocket
    SOURCES
        src/Main.cpp
        src/WsHelpers.cpp
        src/WebSocketManager.cpp
        src/WsClient.cpp
        src/WsServer.cpp
        ../tcp/src/Resolve.cpp
        ../tcp/src/TcpClient.cpp
        ../tcp/src/TcpServer.cpp
        ../tcp/src/TcpManager.cpp
        ../tcp/src/TcpHelpers.cpp
        ../http/src/HttpTls.cpp
)

# Embed the TCP and TLS implementations so websocket.dll has no runtime
# dependency on tcp.dll or http.dll.
target_include_directories(websocket PUBLIC include PRIVATE ../tcp/include ../tcp/src ../http/include ../http/src)
target_compile_definitions(websocket PRIVATE WEBSOCKET_BUILD_SHARED NOMINMAX LODE_TCP_BUILD)
target_link_libraries(websocket PRIVATE $<$<PLATFORM_ID:Windows>:secur32 crypt32>)

if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(websocket PRIVATE LODE_HAS_OPENSSL_TLS)
    target_link_libraries(websocket PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(MSVC)
    target_compile_options(websocket PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(websocket PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(websocket PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(websocket PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
