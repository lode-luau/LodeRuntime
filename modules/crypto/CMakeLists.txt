cmake_minimum_required(VERSION 3.20)
project(crypto LANGUAGES CXX)

if(WIN32)
    set(CRYPTO_PROVIDER_SOURCES src/CryptoCng.cpp src/CryptoAsymmetric.cpp)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    set(CRYPTO_PROVIDER_SOURCES src/CryptoOpenSsl.cpp)
endif()

lode_add_module(crypto
    SOURCES
        src/Main.cpp
        src/CryptoCommon.cpp
        ${CRYPTO_PROVIDER_SOURCES}
)

if(WIN32)
    target_link_libraries(crypto PRIVATE Bcrypt)
else()
    target_link_libraries(crypto PRIVATE OpenSSL::Crypto)
endif()

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
    target_link_options(crypto PRIVATE -flto ${LODE_GC_SECTIONS_LINK_OPTION})
endif()

if(NOT WIN32)
    # The packaged POSIX module carries OpenSSL beside the module. Keep the
    # build tree's dependency path for local tests and make installed modules
    # relocatable through their own directory.
    if(APPLE)
        set(CRYPTO_MODULE_RPATH "@loader_path")
    else()
        set(CRYPTO_MODULE_RPATH "$ORIGIN")
    endif()
    set_target_properties(crypto PROPERTIES
        BUILD_RPATH "${CRYPTO_MODULE_RPATH};$<TARGET_FILE_DIR:OpenSSL::Crypto>"
        INSTALL_RPATH "${CRYPTO_MODULE_RPATH}"
    )
endif()
