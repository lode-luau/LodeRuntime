if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED PROJECT_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "installer_test.cmake requires LODE_EXECUTABLE, PROJECT_SOURCE_DIR, and TEST_BINARY_DIR")
endif()

set(test_root "${PROJECT_SOURCE_DIR}/tests/package/lode-installer-test")
set(unlocked_test_root "${PROJECT_SOURCE_DIR}/tests/package/lode-installer-unlocked-test")
set(dev_test_root "${PROJECT_SOURCE_DIR}/tests/package/lode-installer-dev-test")
set(cache_home "${TEST_BINARY_DIR}/lode-installer-cache")
set(stdlib_fixture "${PROJECT_SOURCE_DIR}/tests/package/stdlib_dependency_consumer")
set(dev_fixture "${PROJECT_SOURCE_DIR}/tests/package/dev_path_consumer")

file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
file(MAKE_DIRECTORY "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")

file(GLOB stdlib_entries "${stdlib_fixture}/*")
file(COPY ${stdlib_entries} DESTINATION "${test_root}")
file(COPY ${stdlib_entries} DESTINATION "${unlocked_test_root}")
file(REMOVE "${unlocked_test_root}/lode.lock")
file(GLOB dev_entries "${dev_fixture}/*")
file(COPY ${dev_entries} DESTINATION "${dev_test_root}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install "${unlocked_test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "unlocked local install failed:\n${output}\n${error}")
endif()

if(NOT EXISTS "${unlocked_test_root}/lode.lock" OR
   NOT EXISTS "${unlocked_test_root}/.config.luau" OR
   NOT EXISTS "${unlocked_test_root}/lode_modules/signal/init.luau" OR
   NOT EXISTS "${unlocked_test_root}/lode_modules/task/init.luau")
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "unlocked local install did not write the lockfile and package view")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" "${unlocked_test_root}/init.luau"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "unlocked local install was not accepted by lode:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked "${unlocked_test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "generated lockfile was not accepted by locked install:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked "${test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "stdlib locked install failed:\n${output}\n${error}")
endif()

if(NOT EXISTS "${test_root}/.config.luau" OR
   NOT EXISTS "${test_root}/lode_modules/signal/init.luau" OR
   NOT EXISTS "${test_root}/lode_modules/task/init.luau")
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "stdlib locked install did not materialize the expected package view")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" ci validate --source --locked "${test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "locked source validation after installation failed:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" ci validate --artifact --locked "${test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "locked artifact validation after installation failed:\n${output}\n${error}")
endif()

file(READ "${test_root}/.config.luau" config_content)
string(FIND "${config_content}" "lode_modules/signal" signal_alias)
string(FIND "${config_content}" "lode_modules/task" task_alias)
if(signal_alias LESS 0 OR task_alias LESS 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "stdlib locked install did not generate both aliases")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" "${test_root}/init.luau"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "generated .config.luau was not accepted by lode:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked "${test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "repeating an unchanged locked install failed:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked "${dev_test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR EXISTS "${dev_test_root}/lode_modules")
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "devDependencies were materialized without --dev:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${cache_home}" "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install --locked --dev "${dev_test_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "locked --dev install failed:\n${output}\n${error}")
endif()

if(NOT EXISTS "${dev_test_root}/.config.luau" OR
   NOT EXISTS "${dev_test_root}/lode_modules/local_signal/init.luau" OR
   NOT EXISTS "${dev_test_root}/lode_modules/task/init.luau")
    file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
    message(FATAL_ERROR "locked --dev install did not materialize the expected development graph")
endif()

file(REMOVE_RECURSE "${test_root}" "${unlocked_test_root}" "${dev_test_root}" "${cache_home}")
