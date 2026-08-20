if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED PROJECT_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "git_installer_test.cmake requires LODE_EXECUTABLE, PROJECT_SOURCE_DIR, and TEST_BINARY_DIR")
endif()

set(git_source "${PROJECT_SOURCE_DIR}/tests/package/lode-git-source")
set(consumer_root "${PROJECT_SOURCE_DIR}/tests/package/lode-git-consumer")
set(cache_home "${TEST_BINARY_DIR}/lode-git-cache")
file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
file(MAKE_DIRECTORY "${git_source}" "${consumer_root}" "${cache_home}")

file(WRITE "${git_source}/lode.json" [=[{
  "name": "git_signal",
  "version": "1.0.0",
  "license": "MIT",
  "dependencies": {
    "task": "1.0.0"
  }
}
]=])
file(WRITE "${git_source}/init.luau" [=[
local task = require("@task")
return { has_task = task ~= nil }
]=])
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
execute_process(COMMAND git -C "${git_source}" add lode.json init.luau LICENSE)
execute_process(
    COMMAND git -C "${git_source}" commit --quiet -m "Add Git package fixture"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "git commit failed:\n${output}\n${error}")
endif()

file(TO_CMAKE_PATH "${git_source}" git_reference)
file(WRITE "${consumer_root}/lode.json" "{\n  \"name\": \"git_consumer\",\n  \"version\": \"1.0.0\",\n  \"license\": \"MIT\",\n  \"dependencies\": {\n    \"git_signal\": {\n      \"git\": \"${git_reference}\",\n      \"version\": \"1.0.0\"\n    }\n  }\n}\n")
file(WRITE "${consumer_root}/init.luau" [=[
local git_signal = require("@git_signal")
assert(git_signal.has_task == true, "Git package did not load its stdlib dependency")
]=])
file(WRITE "${consumer_root}/LICENSE" "MIT\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" install "${consumer_root}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Git local install failed:\n${output}\n${error}")
endif()

if(NOT EXISTS "${consumer_root}/lode.lock" OR
   NOT EXISTS "${consumer_root}/lode_modules/git_signal/init.luau" OR
   NOT EXISTS "${consumer_root}/lode_modules/task/init.luau")
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Git local install did not materialize the resolved graph")
endif()

file(READ "${consumer_root}/lode.lock" lock_content)
string(FIND "${lock_content}" "\"source\": \"git\"" git_source_marker)
string(FIND "${lock_content}" "\"commit\": \"" git_commit_marker)
if(git_source_marker LESS 0 OR git_commit_marker LESS 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Git lockfile entry is missing its source or resolved commit:\n${lock_content}")
endif()

if(EXISTS "${consumer_root}/lode_modules/git_signal/.git")
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Git metadata leaked into the installed package")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" "${consumer_root}/init.luau"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
    message(FATAL_ERROR "Runtime rejected the Git package installation:\n${output}\n${error}")
endif()

file(REMOVE_RECURSE "${git_source}" "${consumer_root}" "${cache_home}")
