cmake_minimum_required(VERSION 3.20)
project(tcp LANGUAGES CXX)

lode_add_module(tcp
    SOURCES
        src/Main.cpp
        src/Resolve.cpp
        src/TcpClient.cpp
        src/TcpServer.cpp
        src/TcpManager.cpp
        src/TcpHelpers.cpp
        ../http/src/HttpTls.cpp
)

target_include_directories(tcp PUBLIC include PRIVATE ../http/include ../http/src)
target_compile_definitions(tcp PRIVATE LODE_TCP_BUILD NOMINMAX)
target_link_libraries(tcp PRIVATE $<$<PLATFORM_ID:Windows>:secur32 crypt32>)

if(NOT WIN32 AND NOT EMSCRIPTEN)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(tcp PRIVATE LODE_HAS_OPENSSL_TLS)
    target_link_libraries(tcp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        set(TCP_MODULE_RPATH "@loader_path")
    else()
        set(TCP_MODULE_RPATH "$ORIGIN")
    endif()
    set_target_properties(tcp PROPERTIES
        BUILD_RPATH "${TCP_MODULE_RPATH};$<TARGET_FILE_DIR:OpenSSL::SSL>"
        INSTALL_RPATH "${TCP_MODULE_RPATH}"
    )
endif()

if(MSVC)
    target_compile_options(tcp PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GR- /GL)
    target_link_options(tcp PRIVATE $<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF> $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
else()
    target_compile_options(tcp PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(tcp PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
