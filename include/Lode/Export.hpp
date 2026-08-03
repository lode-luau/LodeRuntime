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
