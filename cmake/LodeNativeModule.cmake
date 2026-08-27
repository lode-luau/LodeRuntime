include(CMakeParseArguments)

# GNU ld and Apple's ld64 use different dead-stripping options. Keep this
# available to the repository's native targets without forcing GNU flags onto
# macOS builds.
if(APPLE)
    set(LODE_GC_SECTIONS_LINK_OPTION "-Wl,-dead_strip")
else()
    set(LODE_GC_SECTIONS_LINK_OPTION "-Wl,--gc-sections")
endif()

function(lode_get_platform_name output_variable)
    if(WIN32)
        set(${output_variable} windows PARENT_SCOPE)
    elseif(EMSCRIPTEN)
        set(${output_variable} wasm PARENT_SCOPE)
    elseif(ANDROID)
        set(${output_variable} android PARENT_SCOPE)
    elseif(IOS)
        set(${output_variable} ios PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
        set(${output_variable} freebsd PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS|Solaris")
        set(${output_variable} solaris PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_NAME MATCHES "Haiku")
        set(${output_variable} haiku PARENT_SCOPE)
    elseif(APPLE)
        set(${output_variable} macos PARENT_SCOPE)
    else()
        set(${output_variable} linux PARENT_SCOPE)
    endif()
endfunction()

function(lode_get_architecture_name output_variable)
    if(EMSCRIPTEN)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(${output_variable} wasm64 PARENT_SCOPE)
        else()
            set(${output_variable} wasm32 PARENT_SCOPE)
        endif()
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64|AARCH64")
            set(${output_variable} arm64 PARENT_SCOPE)
        else()
            set(${output_variable} x64 PARENT_SCOPE)
        endif()
    else()
        set(${output_variable} x86 PARENT_SCOPE)
    endif()
endfunction()

function(lode_add_native_module target_name)
    set(options)
    set(one_value_arguments)
    set(multi_value_arguments SOURCES)
    cmake_parse_arguments(LODE_MODULE "${options}" "${one_value_arguments}" "${multi_value_arguments}" ${ARGN})

    if(NOT LODE_MODULE_SOURCES)
        message(FATAL_ERROR "lode_add_native_module(${target_name}) requires SOURCES")
    endif()

    add_library(${target_name} SHARED ${LODE_MODULE_SOURCES})
    target_link_libraries(${target_name} PRIVATE Lode::Module)
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    # Native packages are loaded by an exact filename from lode.json. Avoid
    # CMake's default lib prefix on Unix and keep the output beside the
    # package's generated libs/<platform>/<architecture>/<config> tree.
    # On Windows, ARCHIVE_OUTPUT_DIRECTORY also controls the DLL import
    # library; keeping it beside the module avoids collisions with private
    # static dependencies such as libffi.
    set_target_properties(${target_name} PROPERTIES PREFIX "")

    lode_get_platform_name(target_platform)
    lode_get_architecture_name(target_architecture)
    set(native_output_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/${target_platform}/${target_architecture}/$<CONFIG>")

    foreach(configuration_name ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${configuration_name}" configuration_name_upper)
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_name_upper}
                "${CMAKE_CURRENT_SOURCE_DIR}/libs/${target_platform}/${target_architecture}/${configuration_name}"
            LIBRARY_OUTPUT_DIRECTORY_${configuration_name_upper}
                "${CMAKE_CURRENT_SOURCE_DIR}/libs/${target_platform}/${target_architecture}/${configuration_name}"
            ARCHIVE_OUTPUT_DIRECTORY_${configuration_name_upper}
                "${CMAKE_CURRENT_SOURCE_DIR}/libs/${target_platform}/${target_architecture}/${configuration_name}"
        )
    endforeach()

    if(NOT CMAKE_CONFIGURATION_TYPES)
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${native_output_directory}"
            LIBRARY_OUTPUT_DIRECTORY "${native_output_directory}"
            ARCHIVE_OUTPUT_DIRECTORY "${native_output_directory}"
        )
    endif()
endfunction()
