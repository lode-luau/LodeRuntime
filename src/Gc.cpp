// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Gc.hpp"
#include "lua.h"

namespace Lode
{

template <class F>
static int TryGc(State& vm, F&& fn)
{
    if (lua_State* L = vm.GetLuaState())
        return fn(L);
    return 0;
}

void Gc::Collect(State& vm)
{
    TryGc(vm, [](lua_State* L) { lua_gc(L, LUA_GCCOLLECT, 0); return 0; });
}

bool Gc::Step(State& vm, int stepSizeKB)
{
    return TryGc(vm, [&](lua_State* L) { return lua_gc(L, LUA_GCSTEP, stepSizeKB); }) != 0;
}

void Gc::Stop(State& vm)
{
    TryGc(vm, [](lua_State* L) { lua_gc(L, LUA_GCSTOP, 0); return 0; });
}

void Gc::Restart(State& vm)
{
    TryGc(vm, [](lua_State* L) { lua_gc(L, LUA_GCRESTART, 0); return 0; });
}

void Gc::SetGoal(State& vm, int goal)
{
    TryGc(vm, [&](lua_State* L) { lua_gc(L, LUA_GCSETGOAL, goal); return 0; });
}

void Gc::SetStepMultiplier(State& vm, int stepMul)
{
    TryGc(vm, [&](lua_State* L) { lua_gc(L, LUA_GCSETSTEPMUL, stepMul); return 0; });
}

void Gc::SetStepSize(State& vm, int stepSizeKB)
{
    TryGc(vm, [&](lua_State* L) { lua_gc(L, LUA_GCSETSTEPSIZE, stepSizeKB); return 0; });
}

double Gc::UsedMemoryKB(State& vm)
{
    return TryGc(vm, [](lua_State* L) { return lua_gc(L, LUA_GCCOUNT, 0); });
}

bool Gc::IsRunning(State& vm)
{
    return TryGc(vm, [](lua_State* L) { return lua_gc(L, LUA_GCISRUNNING, 0); }) != 0;
}

bool Gc::IsPaused(State& vm)
{
    return TryGc(vm, [](lua_State* L) { return lua_gc(L, LUA_GCISPAUSED, 0); }) != 0;
}

} // namespace Lode
