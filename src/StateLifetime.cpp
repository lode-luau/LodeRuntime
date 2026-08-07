#include "StateLifetime.hpp"
#include "PinnedRef.hpp"
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

PinnedRef::~PinnedRef()
{
    if (L && lifetime && lifetime->alive.load() && refId != LUA_NOREF && refId != LUA_REFNIL)
    {
        lua_unref(L, refId);
    }
}

PinnedRef CaptureRef(lua_State* L, int index)
{
    PinnedRef ref;
    ref.L = lua_mainthread(L);
    ref.lifetime = GetStateLifetime(L);
    if (lua_type(L, index) == LUA_TTHREAD)
        ref.thread = lua_tothread(L, index);
    lua_pushvalue(L, index);
    ref.refId = lua_ref(L, -1);
    lua_pop(L, 1);
    return ref;
}

void PushRef(lua_State* target, const PinnedRef& ref)
{
    if (!target)
        return;
    if (!ref.L || ref.refId == LUA_NOREF || ref.refId == LUA_REFNIL ||
        lua_mainthread(target) != ref.L)
    {
        lua_pushnil(target);
        return;
    }
    lua_getref(ref.L, ref.refId);
    if (target != ref.L)
        lua_xmove(ref.L, target, 1);
}
}
