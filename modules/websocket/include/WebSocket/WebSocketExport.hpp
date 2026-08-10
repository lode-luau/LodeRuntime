// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef WEBSOCKET_BUILD_SHARED
#        define WEBSOCKET_API __declspec(dllexport)
#    else
#        define WEBSOCKET_API __declspec(dllimport)
#    endif
#else
#    define WEBSOCKET_API __attribute__((visibility("default")))
#endif

namespace Lode
{
class State;
}

extern "C" WEBSOCKET_API void RegisterWebSocket(Lode::State& vm);
