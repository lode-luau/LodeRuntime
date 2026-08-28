if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED TEST_BINARY_DIR OR NOT DEFINED LODE_BUILD_DIR OR NOT DEFINED LODE_CONFIGURATION OR
   NOT DEFINED LODE_PLATFORM OR NOT DEFINED LODE_ARCHITECTURE OR NOT DEFINED LODE_GENERATOR OR NOT DEFINED LODE_LIBRARY_SUFFIX)
    message(FATAL_ERROR "project_init_test.cmake requires runtime, build, host target, generator and configuration arguments")
endif()

set(test_root "${TEST_BINARY_DIR}/lode-project-init-test")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(pure_root "${test_root}/pure")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init example_project --description "A generated Luau package" --license MIT "${pure_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init failed for a pure Luau project:\n${output}\n${error}")
endif()
foreach(required_file IN ITEMS package.luau init.luau LICENSE README.md)
    if(NOT EXISTS "${pure_root}/${required_file}")
        message(FATAL_ERROR "lode init did not create ${required_file}")
    endif()
endforeach()
file(READ "${pure_root}/package.luau" pure_manifest)
foreach(required_text IN ITEMS "name = \"example_project\"" "version = \"0.1.0\"" "A generated Luau package")
    string(FIND "${pure_manifest}" "${required_text}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Pure project manifest is missing '${required_text}'")
    endif()
endforeach()
string(FIND "${pure_manifest}" "releaseTargets" position)
if(NOT position LESS 0)
    message(FATAL_ERROR "Pure project manifest must not contain releaseTargets")
endif()
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci validate --source "${pure_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Generated pure project did not validate:\n${output}\n${error}")
endif()

set(legacy_root "${test_root}/legacy")
file(MAKE_DIRECTORY "${legacy_root}")
file(WRITE "${legacy_root}/package.luau"
    "return {\n"
    "    name = \"legacy_project\",\n"
    "    version = \"1.0.0\",\n"
    "    libraries = {},\n"
    "    releaseTargets = {},\n"
    "}\n")
file(WRITE "${legacy_root}/init.luau" "return {}\n")
file(WRITE "${legacy_root}/LICENSE" "MIT\n")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci validate --source "${legacy_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(legacy_validation_output "${output}\n${error}")
if(result EQUAL 0 OR
   NOT legacy_validation_output MATCHES "field 'libraries' is no longer supported" OR
   NOT legacy_validation_output MATCHES "field 'releaseTargets' is no longer supported")
    message(FATAL_ERROR "Legacy manifest fields were accepted or reported incorrectly:\n${legacy_validation_output}")
endif()

set(native_root "${test_root}/native")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init native_project --native --description "A generated native package" --license MIT "${native_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init failed for a native project:\n${output}\n${error}")
endif()
foreach(required_file IN ITEMS package.luau init.luau LICENSE README.md CMakeLists.txt src/main.cpp)
    if(NOT EXISTS "${native_root}/${required_file}")
        message(FATAL_ERROR "lode init did not create native ${required_file}")
    endif()
endforeach()
file(READ "${native_root}/package.luau" native_manifest)
foreach(required_text IN ITEMS "implementation" "${LODE_PLATFORM}/${LODE_ARCHITECTURE}")
    string(FIND "${native_manifest}" "${required_text}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Native project manifest is missing '${required_text}'")
    endif()
endforeach()
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci validate --source "${native_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Generated native project did not validate:\n${output}\n${error}")
endif()

set(lode_root "${test_root}/lode-development")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${LODE_BUILD_DIR}" --config "${LODE_CONFIGURATION}" --component Lode --prefix "${lode_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not install the current Lode distribution for the generated module project:\n${output}\n${error}")
endif()
set(native_configure_command
    "${CMAKE_COMMAND}" -S "${native_root}" -B "${native_root}/build"
    -G "${LODE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${lode_root}")
if(LODE_MULTI_CONFIG)
    list(APPEND native_configure_command "-DCMAKE_CONFIGURATION_TYPES=${LODE_CONFIGURATION}")
else()
    list(APPEND native_configure_command "-DCMAKE_BUILD_TYPE=${LODE_CONFIGURATION}")
endif()
execute_process(
    COMMAND ${native_configure_command}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Generated native project did not configure:\n${output}\n${error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${native_root}/build" --config "${LODE_CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
set(native_library "${native_root}/libs/${LODE_PLATFORM}/${LODE_ARCHITECTURE}/${LODE_CONFIGURATION}/native_project${LODE_LIBRARY_SUFFIX}")
if(NOT result EQUAL 0 OR NOT EXISTS "${native_library}")
    message(FATAL_ERROR "Generated native project did not build for the host:\n${output}\n${error}")
endif()

set(nonempty_root "${test_root}/nonempty")
file(MAKE_DIRECTORY "${nonempty_root}")
file(WRITE "${nonempty_root}/preserve.txt" "do not overwrite\n")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init nonempty_project --description "Should succeed" "${nonempty_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init failed for a non-empty project directory:\n${output}\n${error}")
endif()
if(NOT EXISTS "${nonempty_root}/package.luau")
    message(FATAL_ERROR "lode init did not create package.luau in non-empty directory")
endif()
if(NOT EXISTS "${nonempty_root}/preserve.txt")
    message(FATAL_ERROR "lode init deleted existing file in non-empty directory")
endif()
file(READ "${nonempty_root}/preserve.txt" preserved_content)
if(NOT preserved_content MATCHES "do not overwrite")
    message(FATAL_ERROR "lode init modified existing file in non-empty directory")
endif()

# Test: lode init with no arguments auto-derives name from directory
set(auto_root "${test_root}/auto-project")
file(MAKE_DIRECTORY "${auto_root}")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init
    WORKING_DIRECTORY "${auto_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init with no arguments failed:\n${output}\n${error}")
endif()
if(NOT EXISTS "${auto_root}/package.luau")
    message(FATAL_ERROR "lode init with no arguments did not create package.luau")
endif()
file(READ "${auto_root}/package.luau" auto_manifest)
string(FIND "${auto_manifest}" "name = \"auto-project\"" position)
if(position LESS 0)
    message(FATAL_ERROR "lode init with no arguments did not derive name from directory")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" init "bad/name" "${test_root}/bad-name"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR EXISTS "${test_root}/bad-name/package.luau")
    message(FATAL_ERROR "lode init accepted an unsafe project name")
endif()

file(REMOVE_RECURSE "${test_root}")
