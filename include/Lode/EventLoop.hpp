#pragma once

#include "Lode/Export.hpp"

struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace Lode
{

class State;

class LODE_API EventLoop
{
public:
    static EventLoop& Default();

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void Run(State& vm);
    void Step(State& vm);
    void Stop();

    [[nodiscard]] uv_loop_t* GetUVLoop() const { return loop_; }

private:
    uv_loop_t* loop_ = nullptr;
};

} // namespace Lode
