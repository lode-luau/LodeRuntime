// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/CFunctionCallContext.hpp"

namespace Lode::Detail
{
namespace
{
thread_local CFunctionCallContext g_context;
}

CFunctionCallContext& CurrentCFunctionCallContext()
{
    return g_context;
}

ScopedCFunctionCall::ScopedCFunctionCall(lua_State* state)
    : context_(CurrentCFunctionCallContext()), previousState_(context_.activeState)
{
    context_.activeState = state;
}

ScopedCFunctionCall::~ScopedCFunctionCall()
{
    context_.activeState = previousState_;
}

ScopedForeignCallback::ScopedForeignCallback(void* callback)
    : context_(CurrentCFunctionCallContext()),
      previousDepth_(context_.callbackDepth),
      previousInCallback_(context_.inForeignCallback),
      previousMayYield_(context_.callbackMayYield),
      previousExplicitYield_(context_.explicitYieldRequested),
      previousCallback_(context_.activeCallback)
{
    ++context_.callbackDepth;
    context_.inForeignCallback = true;
    context_.callbackMayYield = false;
    context_.activeCallback = callback;
}

ScopedForeignCallback::~ScopedForeignCallback()
{
    context_.callbackDepth = previousDepth_;
    context_.inForeignCallback = previousInCallback_;
    context_.callbackMayYield = previousMayYield_;
    context_.explicitYieldRequested = previousExplicitYield_;
    context_.activeCallback = previousCallback_;
}

} // namespace Lode::Detail
