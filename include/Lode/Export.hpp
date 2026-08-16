// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

/**
 * @def LODE_API
 * @brief Macro used to export or import classes and functions from the LodeRuntime shared library.
 */

/**
 * @def LODE_EXPORT
 * @brief Macro used to export C-linkage functions (e.g. for native modules).
 */

#if defined(_WIN32)
  #if defined(LODE_CORE_BUILD)
    #define LODE_API __declspec(dllexport)
  #else
    #define LODE_API __declspec(dllimport)
  #endif
  #define LODE_EXPORT extern "C" __declspec(dllexport)
#else
  #define LODE_API __attribute__((visibility("default")))
  #define LODE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/**
 * @def LODE_BUILD_CONFIG_NAME
 * @brief The build configuration (e.g. "Debug"/"Release") that the current
 * binary (LodeCore or a native module) was compiled with.
 *
 * CMake defines this token per target via `$<CONFIG>` so the loader can refuse
 * to initialize a module that was built for a different configuration than the
 * running runtime. Refusing happens before any cross-boundary std::string
 * round-trip, turning a CRT/STL ABI mismatch crash into a clean, named error.
 */
#ifndef LODE_BUILD_CONFIG_NAME
#define LODE_BUILD_CONFIG_NAME LODE_BUILD_CONFIG_UNSET
#endif

#define LODE_STRINGIFY_INNER(x) #x
#define LODE_STRINGIFY(x) LODE_STRINGIFY_INNER(x)

/**
 * @brief Returns the build-configuration name of the current TU as a string
 * literal. Empty here and in a loaded module means "cannot verify".
 */
inline const char* LodeBuildConfigName()
{
    const char* name = LODE_STRINGIFY(LODE_BUILD_CONFIG_NAME);
    return name;
}
