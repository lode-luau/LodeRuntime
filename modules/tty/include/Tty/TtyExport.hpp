// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32
    #ifdef LODE_TTY_BUILD
        #define TTY_API __declspec(dllexport)
    #else
        #define TTY_API __declspec(dllimport)
    #endif
#else
    #define TTY_API __attribute__((visibility("default")))
#endif
