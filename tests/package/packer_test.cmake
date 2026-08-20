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

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive_path}"
    RESULT_VARIABLE result OUTPUT_VARIABLE archive_listing ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
    message(FATAL_ERROR "packed archive could not be listed:\n${error}")
endif()

foreach(required_entry IN ITEMS "lode.json" "init.luau" "LICENSE" "lode_modules/signal/init.luau")
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

file(REMOVE_RECURSE "${package_root}" "${cache_home}" "${TEST_BINARY_DIR}/lode-packer-output")
