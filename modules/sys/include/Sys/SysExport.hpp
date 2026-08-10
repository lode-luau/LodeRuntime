// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32
    #ifdef LODE_SYS_BUILD
        #define SYS_API __declspec(dllexport)
    #else
        #define SYS_API __declspec(dllimport)
    #endif
#else
    #define SYS_API __attribute__((visibility("default")))
#endif
