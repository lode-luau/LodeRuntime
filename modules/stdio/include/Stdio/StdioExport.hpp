// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32
    #ifdef LODE_STDIO_BUILD
        #define STDIO_API __declspec(dllexport)
    #else
        #define STDIO_API __declspec(dllimport)
    #endif
#else
    #define STDIO_API __attribute__((visibility("default")))
#endif
