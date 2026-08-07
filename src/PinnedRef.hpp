#pragma once

#include <memory>

struct lua_State;

namespace Lode::Detail
{
struct StateLifetime;

// Pins a Lua object in the GC through a registry reference. The reference is
// released when the last copy of the PinnedRef dies, provided the owning
// state is still alive.
struct PinnedRef
{
    lua_State* L = nullptr;
    lua_State* thread = nullptr;
    int refId = -1;
    std::shared_ptr<StateLifetime> lifetime;

    // Copying would duplicate the registry reference and double-unref it.
    PinnedRef() = default;
    PinnedRef(const PinnedRef&) = delete;
    PinnedRef& operator=(const PinnedRef&) = delete;
    PinnedRef(PinnedRef&&) = default;
    PinnedRef& operator=(PinnedRef&&) = default;

    ~PinnedRef();
};

// Captures the value at the given stack index into a registry reference on
// the value's main thread. Leaves the stack unchanged.
PinnedRef CaptureRef(lua_State* L, int index);

// Pushes the referenced value onto the target state. Values never cross
// unrelated states: a reference bound to another main thread pushes nil.
void PushRef(lua_State* target, const PinnedRef& ref);
}
