cmake_minimum_required(VERSION 3.20)
project(http LANGUAGES CXX)

lode_add_module(http
    SOURCES
        src/Main.cpp
        src/HttpManager.cpp
        src/HttpClient.cpp
        src/HttpServer.cpp
        src/HttpHelpers.cpp
        src/HttpTls.cpp
)

target_include_directories(http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_link_libraries(http PRIVATE Secur32)
elseif(NOT EMSCRIPTEN)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(http PRIVATE LODE_HAS_OPENSSL_TLS)
    target_link_libraries(http PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        set(HTTP_MODULE_RPATH "@loader_path")
    else()
        set(HTTP_MODULE_RPATH "$ORIGIN")
    endif()
    set_target_properties(http PROPERTIES
        BUILD_RPATH "${HTTP_MODULE_RPATH};$<TARGET_FILE_DIR:OpenSSL::SSL>"
        INSTALL_RPATH "${HTTP_MODULE_RPATH}"
    )
endif()

if(MSVC)
    target_compile_options(http PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O1>
        $<$<NOT:$<CONFIG:Debug>>:/GR->
        $<$<NOT:$<CONFIG:Debug>>:/GL>
    )
    target_link_options(http PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
        $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
        $<$<NOT:$<CONFIG:Debug>>:/LTCG>
    )
else()
    target_compile_options(http PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(http PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()
