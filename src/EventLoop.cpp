// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/EventLoop.hpp"
#include "Lode/State.hpp"
#include "uv.h"
#include <stdexcept>

namespace Lode
{

EventLoop::EventLoop()
{
    loop_ = new uv_loop_t;
    int status = uv_loop_init(loop_);
    if (status != 0)
    {
        delete loop_;
        loop_ = nullptr;
        throw std::runtime_error(std::string("Failed to initialize event loop: ") + uv_strerror(status));
    }
}

EventLoop::~EventLoop()
{
    Close();
}

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

void EventLoop::Close()
{
    if (!loop_)
        return;

    uv_stop(loop_);
    while (uv_loop_alive(loop_))
    {
        uv_run(loop_, UV_RUN_NOWAIT);
    }

    uv_loop_close(loop_);
    delete loop_;
    loop_ = nullptr;
}

} // namespace Lode
