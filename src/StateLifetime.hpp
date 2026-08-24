#pragma once

#include <atomic>
#include <memory>

struct lua_State;

namespace Lode::Detail
{
struct StateLifetime
{
    std::atomic_bool alive{true};
    // Type-erased State::Impl shared by every State view over the same VM,
    // so per-call State construction stays allocation-free. Owned here so it
    // dies with the VM's lifetime registration.
    std::shared_ptr<void> sharedImpl;
    // Weak self-handle: lets GetStateLifetime return an owning shared_ptr
    // from the registry fast path (one atomic incref) without touching the
    // global mutex/map. Weak so the lifetime object itself stays uniquely
    // owned by the registration map.
    std::weak_ptr<StateLifetime> self;
};

// Registry-based O(1) lookup support: publishes the raw lifetime pointer
// under a process-unique lightuserdata key in the VM's registry.
void PublishLifetimePtr(lua_State* L, StateLifetime* lifetime);

std::shared_ptr<StateLifetime> RegisterStateLifetime(lua_State* L);
std::shared_ptr<StateLifetime> GetStateLifetime(lua_State* L);
void InvalidateStateLifetime(lua_State* L);
}
