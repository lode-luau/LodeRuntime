// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"

struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace Lode
{

class State;

/**
 * @brief Represents a libuv-based event loop for asynchronous operations.
 */
class LODE_API EventLoop
{
public:
    /** @brief Retrieves the default, singleton event loop. */
    static EventLoop& Default();

    /** @brief Constructs a new, isolated event loop. */
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /** @brief Runs the event loop continuously until no active events remain. */
    void Run(State& vm);
    /** @brief Runs a single iteration (step) of the event loop. */
    void Step(State& vm);
    /** @brief Stops the event loop. */
    void Stop();

    /** @brief Retrieves the underlying raw libuv loop pointer. */
    [[nodiscard]] uv_loop_t* GetUVLoop() const { return loop_; }

private:
    uv_loop_t* loop_ = nullptr;
};

} // namespace Lode
