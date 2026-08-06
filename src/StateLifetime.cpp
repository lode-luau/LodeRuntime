#include "StateLifetime.hpp"
#include "lua.h"
#include <mutex>
#include <unordered_map>

namespace Lode::Detail
{
namespace
{
std::mutex mutex;
std::unordered_map<lua_State*, std::weak_ptr<StateLifetime>> lifetimes;
}

std::shared_ptr<StateLifetime> RegisterStateLifetime(lua_State* L)
{
    if (!L) return {};
    L = lua_mainthread(L);
    auto lifetime = std::make_shared<StateLifetime>();
    std::lock_guard lock(mutex);
    lifetimes[L] = lifetime;
    return lifetime;
}

std::shared_ptr<StateLifetime> GetStateLifetime(lua_State* L)
{
    if (!L) return {};
    L = lua_mainthread(L);
    std::lock_guard lock(mutex);
    auto it = lifetimes.find(L);
    return it == lifetimes.end() ? std::shared_ptr<StateLifetime>{} : it->second.lock();
}

void InvalidateStateLifetime(lua_State* L)
{
    if (!L) return;
    L = lua_mainthread(L);
    std::lock_guard lock(mutex);
    auto it = lifetimes.find(L);
    if (it != lifetimes.end())
    {
        if (auto lifetime = it->second.lock()) lifetime->alive.store(false);
        lifetimes.erase(it);
    }
}
}
