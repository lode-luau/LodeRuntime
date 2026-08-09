// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#if defined(_WIN32)
  #if defined(LODE_TCP_BUILD)
    #define TCP_API __declspec(dllexport)
  #else
    #define TCP_API __declspec(dllimport)
  #endif
#else
  #define TCP_API __attribute__((visibility("default")))
#endif
