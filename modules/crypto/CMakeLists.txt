cmake_minimum_required(VERSION 3.20)
project(crypto LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "crypto currently supports Windows x64 through the BCrypt/CNG provider only")
endif()

lode_add_native_module(crypto
    SOURCES
        src/Main.cpp
        src/CryptoCommon.cpp
        src/CryptoCng.cpp
        src/CryptoAsymmetric.cpp
)

target_link_libraries(crypto PRIVATE Bcrypt)

target_compile_definitions(crypto PRIVATE CRYPTO_BUILD)

if(MSVC)
    target_compile_options(crypto PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O1>
        /GR-
        /GL
    )
    target_link_options(crypto PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
        $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
        $<$<NOT:$<CONFIG:Debug>>:/LTCG>
    )
else()
    target_compile_options(crypto PRIVATE -Os -fno-rtti -fvisibility=hidden -flto)
    target_link_options(crypto PRIVATE -flto -Wl,--gc-sections)
endif()
