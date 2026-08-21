if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "cli_interaction_test.cmake requires LODE_EXECUTABLE and TEST_BINARY_DIR")
endif()

set(test_root "${TEST_BINARY_DIR}/lode-cli-interaction-test")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(multiline_code "local function add(left, right)\n    return left + right\nend\nprint(add(20, 22))")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" -c "${multiline_code}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "42")
    message(FATAL_ERROR "lode -c did not execute multiline source:\n${output}\n${error}")
endif()

file(WRITE "${test_root}/repl-input.txt" "1 + 2\n.exit\n")
execute_process(
    COMMAND "${LODE_EXECUTABLE}"
    INPUT_FILE "${test_root}/repl-input.txt"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "3")
    message(FATAL_ERROR "lode REPL did not evaluate an expression from stdin:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" --help
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "Commands: init, add, install, pack, ci, help")
    message(FATAL_ERROR "lode --help did not show the command index:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" --version
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "v1.0.0")
    message(FATAL_ERROR "lode --version did not report the runtime version:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" init --help
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT "${output}${error}" MATCHES "Creates a pure Luau project")
    message(FATAL_ERROR "lode init --help did not show command-specific help:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci unsupported
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Unknown lode ci command" OR
   NOT "${output}${error}" MATCHES "lode ci validate")
    message(FATAL_ERROR "Invalid lode ci command did not show contextual help:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${LODE_EXECUTABLE}" install --unsupported
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "--locked requires lode.lock")
    message(FATAL_ERROR "Invalid lode install arguments did not show detailed help:\n${output}\n${error}")
endif()

file(REMOVE_RECURSE "${test_root}")
