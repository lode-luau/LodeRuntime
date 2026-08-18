# Regression and integration tests, driven by CTest.
#
# Every test here runs through the real runtime path (lode_runtime + .luau),
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

function(lode_add_script_test test_name script_path)
    add_test(
        NAME ${test_name}
        COMMAND lode_runtime "${CMAKE_CURRENT_SOURCE_DIR}/${script_path}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endfunction()

lode_add_script_test(integration_core_api        native/core_api.luau)
lode_add_script_test(integration_native_require  native/native_require.luau)
lode_add_script_test(integration_simd_bench      native/simd_bench.luau)
lode_add_script_test(integration_config_mismatch native/config_mismatch.luau)

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
# does not raise a compile or runtime error through lode_runtime itself, so
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
    COMMAND lode_runtime "${CMAKE_SOURCE_DIR}/modules/sanity/run.luau"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
