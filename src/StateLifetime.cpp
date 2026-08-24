#include "StateLifetime.hpp"
#include "PinnedRef.hpp"
#include "lua.h"
#include "lualib.h"
#include <mutex>
#include <unordered_map>

namespace Lode::Detail
{
namespace
{
std::mutex mutex;
std::unordered_map<lua_State*, std::weak_ptr<StateLifetime>> lifetimes;

// Process-unique address used as a lightuserdata registry key. Each VM has
// its own registry, so the same key never collides across runtimes.
char g_lifetimeKey = 0;
}

void PublishLifetimePtr(lua_State* L, StateLifetime* lifetime)
{
    if (!L) return;
    L = lua_mainthread(L);
    lua_pushlightuserdata(L, &g_lifetimeKey);
    lua_pushlightuserdata(L, lifetime);
    lua_settable(L, LUA_REGISTRYINDEX);
}

StateLifetime* PeekLifetimePtr(lua_State* L)
{
    if (!L) return nullptr;
    L = lua_mainthread(L);
    lua_pushlightuserdata(L, &g_lifetimeKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* ptr = static_cast<StateLifetime*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ptr;
}

std::shared_ptr<StateLifetime> RegisterStateLifetime(lua_State* L)
{
    if (!L) return {};
    L = lua_mainthread(L);
    auto lifetime = std::make_shared<StateLifetime>();
    lifetime->self = lifetime;
    {
        std::lock_guard lock(mutex);
        lifetimes[L] = lifetime;
    }
    PublishLifetimePtr(L, lifetime.get());
    return lifetime;
}

std::shared_ptr<StateLifetime> GetStateLifetime(lua_State* L)
{
    if (!L) return {};
    L = lua_mainthread(L);

    // Fast path: registry-published pointer + atomic alive check. One
    // weak_ptr::lock (atomic incref) instead of the global mutex and map.
    // Semantics match the map exactly: after InvalidateStateLifetime the
    // alive flag is false and we return null, same as an erased entry.
    if (auto* cached = PeekLifetimePtr(L))
    {
        if (cached->alive.load(std::memory_order_relaxed))
            return cached->self.lock();
        return {};
    }

    // Fallback for states that were never published.
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
