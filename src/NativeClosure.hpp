#pragma once

#include "Lode/State.hpp"
#include "Lode/CFunctionCallContext.hpp"
#include "lua.h"
#include "lualib.h"
#include <utility>

namespace Lode::Detail
{
// Installs a C++ callable as a Luau closure backed by GC-owned userdata.
// The callable receives a State and the raw lua_State (for StackArgs access).
// The wrapper propagates LUA_YIELD and converts C++ exceptions into Lua
// errors, keeping every closure created by the runtime on the same code path.
template <typename Fn>
Value CreateClosure(lua_State* L, const char* name, Fn&& fn)
{
    struct ClosureData
    {
        Fn func;
    };
    auto* data = static_cast<ClosureData*>(lua_newuserdatadtor(L, sizeof(ClosureData), [](void* ptr) {
        static_cast<ClosureData*>(ptr)->~ClosureData();
    }));
    new (data) ClosureData{ std::forward<Fn>(fn) };

    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!data)
        {
            luaL_error(L, "C++ callback data is unavailable");
            return 0;
        }

        State vm(L);
        try
        {
            Value res = data->func(vm, L);
            if (lua_status(L) == LUA_YIELD)
            {
                auto& context = CurrentCFunctionCallContext();
                const bool requested = context.explicitYieldRequested;
                context.explicitYieldRequested = false;
                if (!requested)
                    return 0;
                return lua_yield(L, 0);
            }
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& e)
        {
            luaL_error(L, "C++ callback exception: %s", e.what());
            return 0;
        }
        catch (...)
        {
            luaL_error(L, "C++ callback threw an unknown exception");
            return 0;
        }
    };

    // The userdata (at -1) is captured as the closure upvalue.
    lua_pushcclosure(L, cfunc, name, 1);
    Value val = Value::FromLuaState(L, -1);
    lua_pop(L, 1);
    return val;
}

// Zero-marshaling variant of CreateClosure: the callable pushes its own
// results directly onto the Lua stack and returns their count. This skips
// the Lode::Value boxing/unboxing entirely and supports multi-value returns,
// which the single-Value CreateClosure path cannot express. Intended for
// hot-path native functions (e.g. a future ffi module) whose results are
// plain stack values.
template <typename Fn>
Value CreateClosureN(lua_State* L, const char* name, Fn&& fn)
{
    // The State wrapper is built ONCE at bind time and reused on every call.
    // Constructing State(lua_State*) per invocation heap-allocates a full
    // Impl (registry, module paths, event-loop pointer), which dominated the
    // per-call cost by two orders of magnitude. Hot-path closures therefore
    // share one non-owning State instance.
    struct ClosureData
    {
        Fn func;
        State cachedVm;
    };
    auto* data = static_cast<ClosureData*>(lua_newuserdatadtor(L, sizeof(ClosureData), [](void* ptr) {
        static_cast<ClosureData*>(ptr)->~ClosureData();
    }));
    new (data) ClosureData{ std::forward<Fn>(fn), State(L) };

    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!data)
        {
            luaL_error(L, "C++ callback data is unavailable");
            return 0;
        }

        try
        {
            const int nresults = data->func(data->cachedVm, L);
            if (lua_status(L) == LUA_YIELD)
            {
                // A synchronous callback is explicitly non-yieldable.  Do
                // not attempt a second lua_yield after a native operation has
                // already reported that status.
                if (CurrentCFunctionCallContext().inForeignCallback &&
                    !CurrentCFunctionCallContext().callbackMayYield)
                {
                    luaL_error(L, "cannot yield from a synchronous foreign callback");
                    return 0;
                }
                auto& context = CurrentCFunctionCallContext();
                const bool requested = context.explicitYieldRequested;
                context.explicitYieldRequested = false;
                if (!requested)
                    return nresults;
                return lua_yield(L, 0);
            }
            return nresults;
        }
        catch (const std::exception& e)
        {
            luaL_error(L, "C++ callback exception: %s", e.what());
            return 0;
        }
        catch (...)
        {
            luaL_error(L, "C++ callback threw an unknown exception");
            return 0;
        }
    };

    lua_pushcclosure(L, cfunc, name, 1);
    Value val = Value::FromLuaState(L, -1);
    lua_pop(L, 1);
    return val;
}
}
