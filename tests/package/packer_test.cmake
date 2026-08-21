if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED PROJECT_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "packer_test.cmake requires LODE_EXECUTABLE, PROJECT_SOURCE_DIR, and TEST_BINARY_DIR")
endif()

set(package_root "${TEST_BINARY_DIR}/lode-packer-test")
set(cache_home "${TEST_BINARY_DIR}/lode-packer-cache")
set(archive_path "${TEST_BINARY_DIR}/lode-packer-output/lode-stdlib_dependency_consumer-1.0.0-windows-x64.zip")
file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
file(MAKE_DIRECTORY "${package_root}" "${cache_home}")

file(GLOB package_entries "${PROJECT_SOURCE_DIR}/tests/package/stdlib_dependency_consumer/*")
file(COPY ${package_entries} DESTINATION "${package_root}")
file(MAKE_DIRECTORY "${package_root}/modules/example" "${package_root}/tests")
file(WRITE "${package_root}/modules/example/init.luau" "return require('./helper')\n")
file(WRITE "${package_root}/modules/example/helper.luau" "return { value = 42 }\n")
string(RANDOM LENGTH 131072 ALPHABET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" large_source)
file(WRITE "${package_root}/modules/example/large.luau" "return [[${large_source}]]\n")
file(WRITE "${package_root}/tests/ignored.luau" "error('test source must not be packaged')\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "packer fixture installation failed:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" pack --output "${archive_path}" "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT EXISTS "${archive_path}" OR
   NOT EXISTS "${archive_path}.sha256")
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "lode pack failed:\n${output}\n${error}")
endif()

file(SHA256 "${archive_path}" expected_sha256)
file(READ "${archive_path}.sha256" checksum_content)
string(SUBSTRING "${checksum_content}" 0 64 actual_sha256)
if(NOT actual_sha256 STREQUAL expected_sha256)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "lode pack wrote an incorrect SHA-256 sidecar")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive_path}"
    RESULT_VARIABLE result OUTPUT_VARIABLE archive_listing ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "packed archive could not be listed:\n${error}")
endif()

foreach(required_entry IN ITEMS
    "lode.json"
    "init.luau"
    "LICENSE"
    "lode_modules/signal/init.luau"
    "modules/example/init.luau"
    "modules/example/helper.luau"
    "modules/example/large.luau")
    string(FIND "${archive_listing}" "${required_entry}" position)
    if(position LESS 0)
        file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
        message(FATAL_ERROR "Packed archive is missing '${required_entry}':\n${archive_listing}")
    endif()
endforeach()

string(FIND "${archive_listing}" ".config.luau" config_position)
if(NOT config_position LESS 0)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "Packed archive contains generated .config.luau:\n${archive_listing}")
endif()

string(FIND "${archive_listing}" "tests/ignored.luau" test_position)
if(NOT test_position LESS 0)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "Packed archive contains a test Luau source:\n${archive_listing}")
endif()

file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
