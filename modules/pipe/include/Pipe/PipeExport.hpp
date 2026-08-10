// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#if defined(_WIN32)
  #if defined(LODE_PIPE_BUILD)
    #define PIPE_API __declspec(dllexport)
  #else
    #define PIPE_API __declspec(dllimport)
  #endif
#else
  #define PIPE_API __attribute__((visibility("default")))
#endif