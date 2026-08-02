// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/EventLoop.hpp"
#include "Lode/State.hpp"
#include "uv.h"

namespace Lode
{

EventLoop& EventLoop::Default()
{
    static EventLoop instance;
    return instance;
}

EventLoop::EventLoop()
{
    loop_ = uv_default_loop();
}

EventLoop::~EventLoop() = default;

void EventLoop::Run(State& vm)
{
    if (loop_)
    {
        uv_run(loop_, UV_RUN_DEFAULT);
    }
}

void EventLoop::Step(State& vm)
{
    if (loop_)
    {
        uv_run(loop_, UV_RUN_NOWAIT);
    }
}

void EventLoop::Stop()
{
    if (loop_)
    {
        uv_stop(loop_);
    }
}

} // namespace Lode
