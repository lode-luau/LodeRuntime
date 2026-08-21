if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED TEST_BINARY_DIR OR NOT DEFINED LODE_BUILD_DIR OR NOT DEFINED LODE_CONFIGURATION)
    message(FATAL_ERROR "project_init_test.cmake requires LODE_EXECUTABLE, TEST_BINARY_DIR, LODE_BUILD_DIR and LODE_CONFIGURATION")
endif()

set(test_root "${TEST_BINARY_DIR}/lode-project-init-test")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(pure_root "${test_root}/pure")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init example_project --description "A generated Luau package" "${pure_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init failed for a pure Luau project:\n${output}\n${error}")
endif()
foreach(required_file IN ITEMS lode.json init.luau LICENSE README.md)
    if(NOT EXISTS "${pure_root}/${required_file}")
        message(FATAL_ERROR "lode init did not create ${required_file}")
    endif()
endforeach()
file(READ "${pure_root}/lode.json" pure_manifest)
foreach(required_text IN ITEMS "\"name\": \"example_project\"" "\"version\": \"0.1.0\"" "A generated Luau package")
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

set(native_root "${test_root}/native")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init native_project --native --description "A generated native package" "${native_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "lode init failed for a native project:\n${output}\n${error}")
endif()
foreach(required_file IN ITEMS lode.json init.luau LICENSE README.md CMakeLists.txt src/main.cpp)
    if(NOT EXISTS "${native_root}/${required_file}")
        message(FATAL_ERROR "lode init did not create native ${required_file}")
    endif()
endforeach()
file(READ "${native_root}/lode.json" native_manifest)
foreach(required_text IN ITEMS "releaseTargets" "\"platform\": \"windows\"" "\"architecture\": \"x64\"")
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

set(sdk_root "${test_root}/sdk")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${LODE_BUILD_DIR}" --config "${LODE_CONFIGURATION}" --component LodeSDK --prefix "${sdk_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not install the current SDK for the generated native project:\n${output}\n${error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${native_root}" -B "${native_root}/build"
        "-DCMAKE_PREFIX_PATH=${sdk_root}"
        "-DCMAKE_CONFIGURATION_TYPES=${LODE_CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Generated native project did not configure:\n${output}\n${error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${native_root}/build" --config "${LODE_CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT EXISTS "${native_root}/libs/windows/x64/${LODE_CONFIGURATION}/native_project.dll")
    message(FATAL_ERROR "Generated native project did not build for the host:\n${output}\n${error}")
endif()

set(nonempty_root "${test_root}/nonempty")
file(MAKE_DIRECTORY "${nonempty_root}")
file(WRITE "${nonempty_root}/preserve.txt" "do not overwrite\n")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" init rejected --description "Must fail" "${nonempty_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR EXISTS "${nonempty_root}/lode.json" OR NOT EXISTS "${nonempty_root}/preserve.txt")
    message(FATAL_ERROR "lode init changed a non-empty project directory")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" init missing_description "${test_root}/missing-description"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR EXISTS "${test_root}/missing-description/lode.json")
    message(FATAL_ERROR "lode init accepted a missing description")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" init --description "Missing project name"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "lode init accepted a missing project name")
endif()

file(REMOVE_RECURSE "${test_root}")
