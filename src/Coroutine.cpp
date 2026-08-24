// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Coroutine.hpp"
#include "Lode/State.hpp"
#include "LuaError.hpp"
#include "PinnedRef.hpp"
#include "lua.h"
#include "lualib.h"
#include "lstate.h"

namespace Lode
{
namespace Detail
{
extern thread_local lua_State* g_moduleLoadCo;
extern thread_local std::vector<Value>* g_moduleLoadSink;
}

namespace
{
    // Creates a new thread on L and pins it through a registry reference.
    std::shared_ptr<Detail::PinnedRef> NewCoroutineRef(lua_State* L)
    {
        lua_newthread(L);
        auto ref = std::make_shared<Detail::PinnedRef>(Detail::CaptureRef(L, -1));
        lua_pop(L, 1);
        return ref;
    }

    // Relaxes the coroutine's call frame so the new arguments fit after a yield.
    void RelaxYieldFrame(lua_State* co, int extra)
    {
        if ((co->status == LUA_YIELD || co->status == LUA_BREAK) && co->top + extra > co->ci->top)
        {
            co->ci->top = co->top + extra;
        }
    }

    // Shared resume tail: checks the resume status and collects the results.
    Result<std::vector<Value>> ResumeCore(lua_State* co, int resumeStatus)
    {
        if (resumeStatus != LUA_OK && resumeStatus != LUA_YIELD)
        {
            const char* msg = lua_tostring(co, -1);
            lua_rawcheckstack(co, 2);
            luaL_traceback(co, co, msg, 1);
            std::string errStr = LuaErrorMessage(co, -1);
            lua_pop(co, 2);
            return Error::Runtime("Coroutine execution error: " + errStr);
        }

        int top = lua_gettop(co);
        std::vector<Value> results;
        results.reserve(top);
        for (int i = 1; i <= top; ++i)
        {
            results.push_back(Value::FromLuaState(co, i));
        }
        lua_pop(co, top);

        // Module-load capture: if this thread is a required module being
        // loaded with the async pump (see State.cpp), hand its return values
        // to the loader's sink instead of dropping them.
        if (Detail::g_moduleLoadCo == co && Detail::g_moduleLoadSink)
        {
            *Detail::g_moduleLoadSink = results;
        }

        return results;
    }
}

Coroutine::Coroutine() = default;

Coroutine::Coroutine(lua_State* L, int fnRef)
{
    if (!L) return;
    refData_ = NewCoroutineRef(L);

    lua_getref(L, fnRef);
    lua_xmove(L, refData_->thread, 1);
}

Coroutine::Coroutine(State& vm, const Value& fn)
{
    lua_State* L = vm.GetLuaState();
    if (!L) return;

    fn.PushToLuaState(L);
    int fnRef = lua_ref(L, -1);
    lua_pop(L, 1);

    refData_ = NewCoroutineRef(L);

    lua_getref(L, fnRef);
    lua_xmove(L, refData_->thread, 1);
    lua_unref(L, fnRef);
}

Coroutine::Coroutine(lua_State* threadState)
{
    if (!threadState) return;
    lua_pushthread(threadState);
    refData_ = std::make_shared<Detail::PinnedRef>(Detail::CaptureRef(threadState, -1));
    lua_pop(threadState, 1);
}

bool Coroutine::IsValid() const
{
    return refData_ && refData_->thread != nullptr;
}

Coroutine::~Coroutine() = default;
Coroutine::Coroutine(const Coroutine& other) = default;
Coroutine::Coroutine(Coroutine&& other) noexcept = default;
Coroutine& Coroutine::operator=(const Coroutine& other) = default;
Coroutine& Coroutine::operator=(Coroutine&& other) noexcept = default;

Result<std::vector<Value>> Coroutine::Resume(const std::vector<Value>& args)
{
    if (!refData_ || !refData_->thread)
    {
        return Error::Runtime("Coroutine is invalid");
    }

    lua_State* co = refData_->thread;
    RelaxYieldFrame(co, static_cast<int>(args.size()));
    for (const auto& arg : args)
    {
        arg.PushToLuaState(co);
    }

    return ResumeCore(co, lua_resume(co, nullptr, static_cast<int>(args.size())));
}

Result<std::vector<Value>> Coroutine::ResumeError(const std::string& errorMsg)
{
    if (!refData_ || !refData_->thread)
    {
        return Error::Runtime("Coroutine is invalid");
    }

    lua_State* co = refData_->thread;
    RelaxYieldFrame(co, 1);
    lua_pushstring(co, errorMsg.c_str());

    return ResumeCore(co, lua_resumeerror(co, nullptr));
}

CoroutineStatus Coroutine::GetStatus() const
{
    if (!refData_ || !refData_->thread) return CoroutineStatus::Dead;
    lua_State* co = refData_->thread;
    int costat = lua_costatus(refData_->L, co);
    switch (costat)
    {
    case LUA_CORUN:
        return CoroutineStatus::Running;
    case LUA_COSUS:
        return CoroutineStatus::Suspended;
    case LUA_CONOR:
        return CoroutineStatus::Normal;
    case LUA_COFIN:
        return CoroutineStatus::Dead;
    case LUA_COERR:
        return CoroutineStatus::Error;
    default:
        return CoroutineStatus::Normal;
    }
}

lua_State* Coroutine::GetThreadState() const
{
    return refData_ ? refData_->thread : nullptr;
}

} // namespace Lode
