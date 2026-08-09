// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#if defined(_WIN32)
  #if defined(LODE_UDP_BUILD)
    #define UDP_API __declspec(dllexport)
  #else
    #define UDP_API __declspec(dllimport)
  #endif
#else
  #define UDP_API __attribute__((visibility("default")))
#endif
