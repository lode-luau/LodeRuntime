# Aggregates all native modules under modules/.
#
# Each module directory that contains its own CMakeLists.txt is added as a
# subdirectory automatically, so adding a new native module no longer requires
# editing the top-level CMakeLists.txt. Nested modules (e.g. filesystem/path)
# are added by their parent module's CMakeLists.txt.
#
# Pure-Luau modules (signal, sanity, toml, ...) have no CMakeLists.txt and are
# intentionally skipped here; they are packaged by the CI as-is.

file(GLOB MODULE_CMAKELISTS "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")

foreach(MODULE_CMAKELIST ${MODULE_CMAKELISTS})
    get_filename_component(MODULE_DIR "${MODULE_CMAKELIST}" DIRECTORY)
    get_filename_component(MODULE_NAME "${MODULE_DIR}" NAME)
    add_subdirectory("${MODULE_NAME}")
endforeach()
