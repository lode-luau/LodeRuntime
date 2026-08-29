// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

struct lua_State;

namespace Lode::Detail
{

/**
 * @brief Thread-local state for synchronous C-function/FFI re-entry.
 *
 * The context is empty on the ordinary runtime path.  C/FFI calls set
 * activeState only while entering C; callbacks use that state to re-enter
 * Luau synchronously without selecting a scheduler coroutine.
 */
struct LODE_API CFunctionCallContext
{
    lua_State* activeState = nullptr;
    void* activeCallback = nullptr;
    unsigned callbackDepth = 0;
    bool inForeignCallback = false;
    bool callbackMayYield = true;
    bool explicitYieldRequested = false;
};

/** @brief Returns the context belonging to the current OS thread. */
LODE_API CFunctionCallContext& CurrentCFunctionCallContext();

/** @brief Installs the active Lua state for one synchronous native call. */
class LODE_API ScopedCFunctionCall
{
public:
    explicit ScopedCFunctionCall(lua_State* state);
    ~ScopedCFunctionCall();

    ScopedCFunctionCall(const ScopedCFunctionCall&) = delete;
    ScopedCFunctionCall& operator=(const ScopedCFunctionCall&) = delete;

private:
    CFunctionCallContext& context_;
    lua_State* previousState_;
};

/** @brief Marks a C callback as synchronous and non-yieldable. */
class LODE_API ScopedForeignCallback
{
public:
    explicit ScopedForeignCallback(void* callback = nullptr);
    ~ScopedForeignCallback();

    ScopedForeignCallback(const ScopedForeignCallback&) = delete;
    ScopedForeignCallback& operator=(const ScopedForeignCallback&) = delete;

private:
    CFunctionCallContext& context_;
    unsigned previousDepth_;
    bool previousInCallback_;
    bool previousMayYield_;
    bool previousExplicitYield_;
    void* previousCallback_;
};

} // namespace Lode::Detail
