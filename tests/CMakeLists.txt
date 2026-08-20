# Regression and integration tests, driven by CTest.
#
# Every test here runs through the real runtime path (lode + .luau),
# never a standalone probe that links Luau a second time. Two kinds of tests
# live under this directory:
#
#   tests/native/       Native module fixtures (native_module, simd_module)
#                        plus the Luau scripts that exercise them. These
#                        fixtures also back regression tests inside
#                        modules/sanity/tests/ (lifecycle, callable) and the
#                        Luau microbenchmarks under bench/.
#   tests/diagnostics/   Scripts that are expected to fail in a specific,
#                        well-known way (syntax error, type error, runtime
#                        error, ...), used to regression-test the Logger's
#                        diagnostic reporting.
#
# Run with: ctest --test-dir build -C Debug --output-on-failure

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/native_module/CMakeLists.txt")
    add_subdirectory(native/native_module)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/simd_module/CMakeLists.txt")
    add_subdirectory(native/simd_module)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/config_mismatch_module/CMakeLists.txt")
    add_subdirectory(native/config_mismatch_module)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/abi_mismatch_module/CMakeLists.txt")
    add_subdirectory(native/abi_mismatch_module)
endif()

function(lode_add_script_test test_name script_path)
    add_test(
        NAME ${test_name}
        COMMAND lode "${CMAKE_CURRENT_SOURCE_DIR}/${script_path}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endfunction()

lode_add_script_test(integration_core_api        native/core_api.luau)
lode_add_script_test(integration_native_require  native/native_require.luau)
lode_add_script_test(integration_simd_bench      native/simd_bench.luau)
lode_add_script_test(integration_config_mismatch native/config_mismatch.luau)
lode_add_script_test(integration_abi_mismatch  native/abi_mismatch.luau)

# Package validation must reject malformed native artifact paths before any
# package can be published.
add_test(
    NAME package_validator_rejects_path_escape
    COMMAND lode ci validate "${CMAKE_CURRENT_SOURCE_DIR}/native/path_escape_module"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_validator_rejects_path_escape PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME package_validator_accepts_native_source
    COMMAND lode ci validate --source "${CMAKE_CURRENT_SOURCE_DIR}/native/source_package"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_accepts_stdlib_signal
    COMMAND lode ci validate --source "${CMAKE_SOURCE_DIR}/modules/signal"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_rejects_dependency_alias_field
    COMMAND lode ci validate --source "${CMAKE_CURRENT_SOURCE_DIR}/package/invalid_dependency"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_validator_rejects_dependency_alias_field PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME package_validator_rejects_stdlib_version_mismatch
    COMMAND lode ci validate --source "${CMAKE_CURRENT_SOURCE_DIR}/package/stdlib_catalog/modules/consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_validator_rejects_stdlib_version_mismatch PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME package_validator_accepts_path_dependency
    COMMAND lode ci validate --source "${CMAKE_CURRENT_SOURCE_DIR}/package/path_consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_accepts_root_dev_dependency
    COMMAND lode ci validate --source "${CMAKE_CURRENT_SOURCE_DIR}/package/dev_path_consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_accepts_root_dev_lockfile
    COMMAND lode ci validate --source --locked "${CMAKE_CURRENT_SOURCE_DIR}/package/dev_path_consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_accepts_stdlib_lockfile
    COMMAND lode ci validate --source --locked "${CMAKE_CURRENT_SOURCE_DIR}/package/stdlib_dependency_consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME package_validator_rejects_missing_lockfile
    COMMAND lode ci validate --source --locked "${CMAKE_CURRENT_SOURCE_DIR}/package/path_consumer"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_validator_rejects_missing_lockfile PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME package_installer_local_and_locked
    COMMAND ${CMAKE_COMMAND}
        -DLODE_EXECUTABLE=$<TARGET_FILE:lode>
        -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/tests/package/installer_test.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_installer_local_and_locked PROPERTIES RUN_SERIAL TRUE)

add_test(
    NAME package_installer_git_local
    COMMAND ${CMAKE_COMMAND}
        -DLODE_EXECUTABLE=$<TARGET_FILE:lode>
        -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/tests/package/git_installer_test.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_installer_git_local PROPERTIES RUN_SERIAL TRUE)

add_test(
    NAME package_installer_git_native_artifact_boundary
    COMMAND ${CMAKE_COMMAND}
        -DLODE_EXECUTABLE=\$<TARGET_FILE:lode>
        -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/tests/package/git_native_artifact_boundary_test.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_installer_git_native_artifact_boundary PROPERTIES RUN_SERIAL TRUE)

add_test(
    NAME package_installer_git_add_and_tag_resolution
    COMMAND ${CMAKE_COMMAND}
        -DLODE_EXECUTABLE=$<TARGET_FILE:lode>
        -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/tests/package/git_add_test.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_installer_git_add_and_tag_resolution PROPERTIES RUN_SERIAL TRUE)

add_test(
    NAME package_ci_generator
    COMMAND ${CMAKE_COMMAND}
        -DLODE_EXECUTABLE=$<TARGET_FILE:lode>
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P "${CMAKE_SOURCE_DIR}/tests/package/ci_generator_test.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(package_ci_generator PROPERTIES RUN_SERIAL TRUE)

# Exercises State destruction while filesystem resources are still active.
# The script deliberately raises after opening them, so the runtime must run
# module shutdown hooks without hanging or crashing.
lode_add_script_test(regression_filesystem_shutdown diagnostics/filesystem_shutdown.luau)
set_tests_properties(regression_filesystem_shutdown PROPERTIES WILL_FAIL TRUE TIMEOUT 10)

# Diagnostics scripts are expected to fail (non-zero exit) with a specific,
# well-known error category; WILL_FAIL flips the pass/fail interpretation.
#
# duplicate_cond.luau is intentionally excluded: it demonstrates a lint-only
# warning (duplicate branch condition) that the LSP/`luau-analyze` flags, but
# does not raise a compile or runtime error through lode itself, so
# it has no pass/fail exit code to assert on here.
set(LODE_DIAGNOSTIC_SCRIPTS
    syntax
    type
    func_type
    non_string
    unreachable
    runtime
)
foreach(script ${LODE_DIAGNOSTIC_SCRIPTS})
    lode_add_script_test(diagnostics_${script} "diagnostics/${script}.luau")
    set_tests_properties(diagnostics_${script} PROPERTIES WILL_FAIL TRUE)
endforeach()

# The sanity regression suite (modules/sanity), also runnable directly via
# CTest alongside everything else in this directory.
add_test(
    NAME sanity_suite
    COMMAND lode "${CMAKE_SOURCE_DIR}/modules/sanity/run.luau"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
