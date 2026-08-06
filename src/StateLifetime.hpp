#pragma once

#include <atomic>
#include <memory>

struct lua_State;

namespace Lode::Detail
{
struct StateLifetime
{
    std::atomic_bool alive{true};
};

std::shared_ptr<StateLifetime> RegisterStateLifetime(lua_State* L);
std::shared_ptr<StateLifetime> GetStateLifetime(lua_State* L);
void InvalidateStateLifetime(lua_State* L);
}
