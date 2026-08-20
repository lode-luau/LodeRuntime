if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED PROJECT_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "git_native_artifact_boundary_test.cmake requires LODE_EXECUTABLE, PROJECT_SOURCE_DIR, and TEST_BINARY_DIR")
endif()

set(git_source "${PROJECT_SOURCE_DIR}/tests/package/lode-git-native-source")
set(consumer_root "${PROJECT_SOURCE_DIR}/tests/package/lode-git-native-consumer")
set(cache_home "${TEST_BINARY_DIR}/lode-git-native-cache")
file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
file(MAKE_DIRECTORY "${git_source}" "${consumer_root}" "${cache_home}")

file(WRITE "${git_source}/lode.json" [=[{
  "name": "git_native_fixture",
  "version": "1.0.0",
  "license": "MIT",
  "libraries": {
    "windows": {
      "x64": "libs/windows/x64/git_native_fixture.dll"
    }
  }
}
]=])
file(WRITE "${git_source}/init.luau" "return {}\n")
file(WRITE "${git_source}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)\nproject(git_native_fixture LANGUAGES CXX)\n")
file(WRITE "${git_source}/LICENSE" "MIT\n")

execute_process(
    COMMAND git init --quiet "${git_source}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "git init failed:\n${output}\n${error}")
endif()
execute_process(COMMAND git -C "${git_source}" config user.email "lode-tests@example.invalid")
execute_process(COMMAND git -C "${git_source}" config user.name "Lode Tests")
execute_process(COMMAND git -C "${git_source}" add lode.json init.luau CMakeLists.txt LICENSE)
execute_process(
    COMMAND git -C "${git_source}" commit --quiet -m "Add native Git fixture"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "git commit failed:\n${output}\n${error}")
endif()

file(TO_CMAKE_PATH "${git_source}" git_reference)
file(WRITE "${consumer_root}/lode.json" "{\n  \"name\": \"git_native_consumer\",\n  \"version\": \"1.0.0\",\n  \"license\": \"MIT\",\n  \"dependencies\": {\n    \"git_native_fixture\": {\n      \"git\": \"${git_reference}\",\n      \"version\": \"1.0.0\"\n    }\n  }\n}\n")
file(WRITE "${consumer_root}/init.luau" "return {}\n")
file(WRITE "${consumer_root}/LICENSE" "MIT\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install "${consumer_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "A local native Git package unexpectedly installed without a GitHub Release artifact.")
endif()

set(combined_output "${output}\n${error}")
string(FIND "${combined_output}" "supported GitHub repository reference" marker)
if(marker LESS 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Native Git rejection did not identify the GitHub Release boundary:\n${combined_output}")
endif()

if(EXISTS "${consumer_root}/lode_modules" OR EXISTS "${consumer_root}/lode.lock")
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Failed native artifact resolution left install state behind.")
endif()

file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
