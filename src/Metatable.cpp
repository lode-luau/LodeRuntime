// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Metatable.hpp"
#include "Lode/State.hpp"
#include "NativeCallback.hpp"
#include "LuaError.hpp"
#include "lua.h"
#include <string>
#include <utility>
#include <vector>

namespace Lode
{
namespace
{
    // Installs a typed C++ callable as a metamethod closure. `adapter` turns
    // the raw call frame into the typed call; C++ exceptions become Lua errors
    // tagged with the metamethod name.
    template <typename Fn, typename Adapter>
    void InstallMetamethod(const Table& table, const char* name, Fn&& fn, Adapter&& adapter)
    {
        lua_State* L = table.GetLuaState();
        if (!L) return;

        struct ClosureData
        {
            std::decay_t<Fn> func;
            const char* label;
            std::decay_t<Adapter> adapter;
        };

        auto cfunc = [](lua_State* L) -> int {
            auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
            try
            {
                State vm(L);
                return data->adapter(vm, data->func, L);
            }
            catch (const std::exception& error)
            {
                std::string msg = std::string("C++ ") + data->label + " callback exception";
                return RaiseCppException(L, msg.c_str(), error);
            }
            catch (...)
            {
                std::string msg = std::string("C++ ") + data->label + " callback threw an unknown exception";
                return RaiseCppException(L, msg.c_str());
            }
        };

        table.PushToLuaState(L);
        auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ std::forward<Fn>(fn), name, std::forward<Adapter>(adapter) });
        (void)data;
        lua_pushcclosure(L, cfunc, name, 1);
        lua_setfield(L, -2, name);
        lua_pop(L, 1);
    }
} // namespace

Metatable::Metatable(State& vm) : table_(vm.CreateTable())
{
}

Metatable::~Metatable() = default;
Metatable::Metatable(const Metatable& other) = default;
Metatable::Metatable(Metatable&& other) noexcept = default;
Metatable& Metatable::operator=(const Metatable& other) = default;
Metatable& Metatable::operator=(Metatable&& other) noexcept = default;

void Metatable::SetIndexTable(const Table& targetTable)
{
    table_.Set("__index", Value(targetTable));
}

void Metatable::SetIndexFunction(const std::function<Value(State& vm, Value key)>& fn)
{
    InstallMetamethod(table_, "__index", fn,
        [](State& vm, const std::function<Value(State&, Value)>& func, lua_State* L) -> int {
            Value key = Value::FromLuaState(L, 2);
            func(vm, key).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetNewIndexFunction(const std::function<void(State& vm, Value key, Value val)>& fn)
{
    InstallMetamethod(table_, "__newindex", fn,
        [](State& vm, const std::function<void(State&, Value, Value)>& func, lua_State* L) -> int {
            Value key = Value::FromLuaState(L, 2);
            Value val = Value::FromLuaState(L, 3);
            func(vm, key, val);
            return 0;
        });
}

void Metatable::SetToString(const std::function<std::string(State& vm)>& fn)
{
    InstallMetamethod(table_, "__tostring", fn,
        [](State& vm, const std::function<std::string(State&)>& func, lua_State* L) -> int {
            std::string str = func(vm);
            lua_pushlstring(L, str.data(), str.length());
            return 1;
        });
}

void Metatable::SetGC(const std::function<void(State& vm)>& fn)
{
    // Luau does not run metatable __gc methods. Use a tagged userdata destructor
    // at allocation time for native object lifetime management instead.
    (void)fn;
}

void Metatable::SetCall(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn)
{
    InstallMetamethod(table_, "__call", fn,
        [](State& vm, const std::function<Value(State&, const std::vector<Value>&)>& func, lua_State* L) -> int {
            int top = lua_gettop(L);
            std::vector<Value> args;
            args.reserve(top - 1);
            for (int i = 2; i <= top; ++i)
            {
                args.push_back(Value::FromLuaState(L, i));
            }
            func(vm, args).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetAdd(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__add", fn,
        [](State& vm, const std::function<Value(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            func(vm, a, b).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetSub(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__sub", fn,
        [](State& vm, const std::function<Value(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            func(vm, a, b).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetMul(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__mul", fn,
        [](State& vm, const std::function<Value(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            func(vm, a, b).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetDiv(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__div", fn,
        [](State& vm, const std::function<Value(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            func(vm, a, b).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetIntegerDivide(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__idiv", fn,
        [](State& vm, const std::function<Value(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            func(vm, a, b).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetLength(const std::function<Value(State& vm, Value object)>& fn)
{
    InstallMetamethod(table_, "__len", fn,
        [](State& vm, const std::function<Value(State&, Value)>& func, lua_State* L) -> int {
            Value object = Value::FromLuaState(L, 1);
            func(vm, object).PushToLuaState(L);
            return 1;
        });
}

void Metatable::SetIter(const std::function<std::vector<Value>(State& vm, Value object)>& fn)
{
    InstallMetamethod(table_, "__iter", fn,
        [](State& vm, const std::function<std::vector<Value>(State&, Value)>& func, lua_State* L) -> int {
            Value object = Value::FromLuaState(L, 1);
            std::vector<Value> results = func(vm, object);
            for (const auto& result : results)
                result.PushToLuaState(L);
            return static_cast<int>(results.size());
        });
}

void Metatable::SetEq(const std::function<bool(State& vm, Value a, Value b)>& fn)
{
    InstallMetamethod(table_, "__eq", fn,
        [](State& vm, const std::function<bool(State&, Value, Value)>& func, lua_State* L) -> int {
            Value a = Value::FromLuaState(L, 1);
            Value b = Value::FromLuaState(L, 2);
            lua_pushboolean(L, func(vm, a, b) ? 1 : 0);
            return 1;
        });
}

void Metatable::SetMetaMethod(const std::string& name, const Value& val)
{
    table_.Set(name, val);
}

} // namespace Lode
