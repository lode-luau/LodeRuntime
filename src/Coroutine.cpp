#include "Lode/Coroutine.hpp"
#include "lua.h"
#include "lualib.h"
#include <stdexcept>

namespace Lode
{

struct Coroutine::RefData
{
    lua_State* mainL = nullptr;
    lua_State* coL = nullptr;
    int threadRef = -1;

    ~RefData()
    {
        if (mainL && threadRef != LUA_NOREF && threadRef != LUA_REFNIL)
        {
            lua_unref(mainL, threadRef);
        }
    }
};

Coroutine::Coroutine() = default;

Coroutine::Coroutine(lua_State* L, int fnRef)
{
    if (!L) return;
    refData_ = std::make_shared<RefData>();
    refData_->mainL = L;
    refData_->coL = lua_newthread(L);
    refData_->threadRef = lua_ref(L, -1);
    lua_pop(L, 1);

    lua_getref(L, fnRef);
    lua_xmove(L, refData_->coL, 1);
}

Coroutine::~Coroutine() = default;
Coroutine::Coroutine(const Coroutine& other) = default;
Coroutine::Coroutine(Coroutine&& other) noexcept = default;
Coroutine& Coroutine::operator=(const Coroutine& other) = default;
Coroutine& Coroutine::operator=(Coroutine&& other) noexcept = default;

Result<std::vector<Value>> Coroutine::Resume(const std::vector<Value>& args)
{
    if (!refData_ || !refData_->coL)
    {
        return Error::Runtime("Coroutine is invalid");
    }

    lua_State* co = refData_->coL;
    for (const auto& arg : args)
    {
        arg.PushToLuaState(co);
    }

    int res = lua_resume(co, nullptr, static_cast<int>(args.size()));
    if (res != LUA_OK && res != LUA_YIELD)
    {
        std::string errStr = lua_tostring(co, -1);
        lua_pop(co, 1);
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

    return results;
}

CoroutineStatus Coroutine::GetStatus() const
{
    if (!refData_ || !refData_->coL) return CoroutineStatus::Dead;
    lua_State* co = refData_->coL;
    int costat = lua_costatus(refData_->mainL, co);
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
    return refData_ ? refData_->coL : nullptr;
}

} // namespace Lode
