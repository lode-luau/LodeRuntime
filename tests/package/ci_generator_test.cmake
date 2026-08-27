if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "ci_generator_test.cmake requires LODE_EXECUTABLE and TEST_BINARY_DIR")
endif()

set(package_root "${TEST_BINARY_DIR}/ci-generator-package")
file(REMOVE_RECURSE "${package_root}")
file(MAKE_DIRECTORY "${package_root}")

file(WRITE "${package_root}/package.luau" [=[return {
  name = "ci_generator_package",
  version = "1.0.0",
  license = "MIT",
  implementation = {
    artifact = "ci_generator_package",
    required = true,
    targets = {
      build = { "windows/x64" },
      release = { "windows/x64" },
    },
  },
  dependencies = {
    example = {
      git = "https://github.com/example/example.git",
      version = "1.0.0",
    },
  },
}
]=])
file(WRITE "${package_root}/init.luau" "return {}\n")
file(WRITE "${package_root}/LICENSE" "MIT\n")
file(WRITE "${package_root}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)\nproject(ci_generator_package LANGUAGES CXX)\n")

execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci init "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci init unexpectedly accepted an unpinned Lode version:\n${output}\n${error}")
endif()
if(EXISTS "${package_root}/.github/workflows/lode.yml")
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci init left a workflow after rejecting an unpinned Lode version")
endif()

set(lode_version "1.0.0-nightly.20260820.2")
set(lode_sha256 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci init
        --lode-version "${lode_version}"
        --lode-sha256 "${lode_sha256}"
        "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci init failed with a valid Lode pin:\n${output}\n${error}")
endif()

set(workflow_path "${package_root}/.github/workflows/lode.yml")
file(READ "${workflow_path}" workflow)
foreach(required_text IN ITEMS
    "LODE_VERSION: \"${lode_version}\""
    "LODE_SHA256: \"${lode_sha256}\""
    "LODE_PACKAGE_NAME: \"ci_generator_package\""
    "LODE_PACKAGE_VERSION: \"1.0.0\""
    "install --dev --locked ."
    "ci validate --source --locked ."
    "ci validate --artifact --locked ."
    "pack --output $archive .")
    string(FIND "${workflow}" "${required_text}" position)
    if(position LESS 0)
        file(REMOVE_RECURSE "${package_root}")
        message(FATAL_ERROR "Generated workflow is missing '${required_text}':\n${workflow}")
    endif()
endforeach()
foreach(placeholder IN ITEMS "<nightly-version>" "<lode-sha256>")
    string(FIND "${workflow}" "${placeholder}" position)
    if(NOT position LESS 0)
        file(REMOVE_RECURSE "${package_root}")
        message(FATAL_ERROR "Generated workflow contains placeholder '${placeholder}'")
    endif()
endforeach()

file(APPEND "${workflow_path}" "\n# User-owned workflow content\n")
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci update "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci update failed for a pinned workflow:\n${output}\n${error}")
endif()

file(READ "${workflow_path}" workflow)
string(FIND "${workflow}" "# User-owned workflow content" user_content_position)
if(user_content_position LESS 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci update removed user-owned workflow content")
endif()
string(FIND "${workflow}" "LODE_VERSION: \"${lode_version}\"" position)
if(position LESS 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci update did not preserve the pinned Lode version")
endif()

string(FIND "${workflow}" "module-linux-x64" linux_job_position)
if(NOT linux_job_position LESS 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "Generated workflow inferred an undeclared linux release target")
endif()

file(WRITE "${package_root}/package.luau" [=[return {
  name = "ci_generator_package",
  version = "1.0.0",
  license = "MIT",
  implementation = {
    artifact = "ci_generator_package",
    required = true,
    targets = {
      build = { "linux/x64" },
      release = { "linux/x64" },
    },
  },
}
]=])
execute_process(
    COMMAND "${LODE_EXECUTABLE}" ci update "${package_root}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci update accepted a target without an implemented runner/platform matrix")
endif()
string(FIND "${output}${error}" "no runner and platform matrix" unsupported_target_message)
if(unsupported_target_message LESS 0)
    file(REMOVE_RECURSE "${package_root}")
    message(FATAL_ERROR "ci update did not report the unsupported target clearly:\n${output}\n${error}")
endif()

file(REMOVE_RECURSE "${package_root}")
