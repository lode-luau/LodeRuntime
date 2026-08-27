if(NOT DEFINED LODE_EXECUTABLE OR NOT DEFINED PROJECT_SOURCE_DIR OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "git_add_test.cmake requires LODE_EXECUTABLE, PROJECT_SOURCE_DIR, and TEST_BINARY_DIR")
endif()

set(git_source "${PROJECT_SOURCE_DIR}/tests/package/lode-git-add-source")
set(latest_consumer "${PROJECT_SOURCE_DIR}/tests/package/lode-git-add-latest-consumer")
set(exact_consumer "${PROJECT_SOURCE_DIR}/tests/package/lode-git-add-exact-consumer")
set(cache_home "${TEST_BINARY_DIR}/lode-git-add-cache")
file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
file(MAKE_DIRECTORY "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")

file(WRITE "${git_source}/package.luau" [=[return {
  name = "git_add_source",
  version = "1.0.0",
  license = "MIT",
}
]=])
file(WRITE "${git_source}/init.luau" "return { version = \"1.0.0\" }\n")
file(WRITE "${git_source}/LICENSE" "MIT\n")

execute_process(COMMAND git init --quiet "${git_source}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "git init failed:\n${output}\n${error}")
endif()
execute_process(COMMAND git -C "${git_source}" config user.email "lode-tests@example.invalid")
execute_process(COMMAND git -C "${git_source}" config user.name "Lode Tests")
execute_process(COMMAND git -C "${git_source}" add package.luau init.luau LICENSE)
execute_process(COMMAND git -c commit.gpgSign=false -C "${git_source}" commit --quiet -m "Add first package release"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "first Git commit failed:\n${output}\n${error}")
endif()
execute_process(COMMAND git -C "${git_source}" tag v1.0.0
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "first Git tag failed:\n${output}\n${error}")
endif()

file(WRITE "${git_source}/package.luau" [=[return {
  name = "git_add_source",
  version = "1.2.0",
  license = "MIT",
}
]=])
file(WRITE "${git_source}/init.luau" "return { version = \"1.2.0\" }\n")
execute_process(COMMAND git -C "${git_source}" add package.luau init.luau)
execute_process(COMMAND git -c commit.gpgSign=false -C "${git_source}" commit --quiet -m "Add latest package release"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "latest Git commit failed:\n${output}\n${error}")
endif()
execute_process(COMMAND git -C "${git_source}" tag v1.2.0
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "latest Git tag failed:\n${output}\n${error}")
endif()

file(WRITE "${latest_consumer}/package.luau" [=[return {
  name = "git_add_latest_consumer",
  version = "1.0.0",
  license = "MIT",
}
]=])
file(WRITE "${latest_consumer}/init.luau" [=[
local package = require("@lode-git-add-source")
assert(package.version == "1.2.0", "lode add without @ did not select the latest stable tag")
]=])
file(WRITE "${latest_consumer}/LICENSE" "MIT\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" add "${git_source}" "${latest_consumer}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
    message(FATAL_ERROR "lode add latest failed:\n${output}\n${error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" "${latest_consumer}/init.luau"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
    message(FATAL_ERROR "latest package did not run after lode add:\n${output}\n${error}")
endif()
file(READ "${latest_consumer}/package.luau" latest_manifest)
string(FIND "${latest_manifest}" "version\"] = \"1.2.0\"" latest_version)
if(latest_version LESS 0)
    file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
    message(FATAL_ERROR "lode add did not write the selected latest version:\n${latest_manifest}")
endif()

file(WRITE "${exact_consumer}/package.luau" [=[return {
  name = "git_add_exact_consumer",
  version = "1.0.0",
  license = "MIT",
}
]=])
file(WRITE "${exact_consumer}/init.luau" [=[
local package = require("@lode-git-add-source")
assert(package.version == "1.0.0", "lode add @version did not select the exact tag")
]=])
file(WRITE "${exact_consumer}/LICENSE" "MIT\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" add "${git_source}@1.0.0" "${exact_consumer}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
    message(FATAL_ERROR "lode add exact version failed:\n${output}\n${error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "USERPROFILE=${cache_home}"
        "${LODE_EXECUTABLE}" "${exact_consumer}/init.luau"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
    message(FATAL_ERROR "exact package did not run after lode add:\n${output}\n${error}")
endif()

file(REMOVE_RECURSE "${git_source}" "${latest_consumer}" "${exact_consumer}" "${cache_home}")
