// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Metatable.hpp"
#include "Lode/State.hpp"
#include "NativeCallback.hpp"
#include "LuaError.hpp"
#include "lua.h"

namespace Lode
{

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
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData
    {
        std::function<Value(State& vm, Value key)> func;
    };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value key = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value res = data->func(vm, key);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __index callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __index callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__index", 1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

void Metatable::SetNewIndexFunction(const std::function<void(State& vm, Value key, Value val)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData
    {
        std::function<void(State& vm, Value key, Value val)> func;
    };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value key = Value::FromLuaState(L, 2);
        Value val = Value::FromLuaState(L, 3);
        try
        {
            State vm(L);
            data->func(vm, key, val);
            return 0;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __newindex callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __newindex callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__newindex", 1);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);
}

void Metatable::SetToString(const std::function<std::string(State& vm)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData
    {
        std::function<std::string(State& vm)> func;
    };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        try
        {
            State vm(L);
            std::string str = data->func(vm);
            lua_pushlstring(L, str.data(), str.length());
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __tostring callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __tostring callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__tostring", 1);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}

void Metatable::SetGC(const std::function<void(State& vm)>& fn)
{
    // Luau does not run metatable __gc methods. Use a tagged userdata destructor
    // at allocation time for native object lifetime management instead.
    (void)fn;
}

void Metatable::SetCall(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData
    {
        std::function<Value(State& vm, const std::vector<Value>& args)> func;
    };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        int top = lua_gettop(L);
        std::vector<Value> args;
        for (int i = 2; i <= top; ++i)
        {
            args.push_back(Value::FromLuaState(L, i));
        }
        try
        {
            State vm(L);
            Value res = data->func(vm, args);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __call callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __call callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__call", 1);
    lua_setfield(L, -2, "__call");
    lua_pop(L, 1);
}

void Metatable::SetAdd(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value res = data->func(vm, a, b);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __add callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __add callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__add", 1);
    lua_setfield(L, -2, "__add");
    lua_pop(L, 1);
}

void Metatable::SetSub(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value res = data->func(vm, a, b);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __sub callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __sub callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__sub", 1);
    lua_setfield(L, -2, "__sub");
    lua_pop(L, 1);
}

void Metatable::SetMul(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value res = data->func(vm, a, b);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __mul callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __mul callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__mul", 1);
    lua_setfield(L, -2, "__mul");
    lua_pop(L, 1);
}

void Metatable::SetDiv(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value res = data->func(vm, a, b);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __div callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __div callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__div", 1);
    lua_setfield(L, -2, "__div");
    lua_pop(L, 1);
}

void Metatable::SetIntegerDivide(const std::function<Value(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            Value result = data->func(vm, a, b);
            result.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __idiv callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __idiv callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__idiv", 1);
    lua_setfield(L, -2, "__idiv");
    lua_pop(L, 1);
}

void Metatable::SetLength(const std::function<Value(State& vm, Value object)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<Value(State&, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value object = Value::FromLuaState(L, 1);
        try
        {
            State vm(L);
            Value result = data->func(vm, object);
            result.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __len callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __len callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__len", 1);
    lua_setfield(L, -2, "__len");
    lua_pop(L, 1);
}

void Metatable::SetIter(const std::function<std::vector<Value>(State& vm, Value object)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<std::vector<Value>(State&, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value object = Value::FromLuaState(L, 1);
        try
        {
            State vm(L);
            std::vector<Value> results = data->func(vm, object);
            for (const auto& result : results)
                result.PushToLuaState(L);
            return static_cast<int>(results.size());
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __iter callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __iter callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__iter", 1);
    lua_setfield(L, -2, "__iter");
    lua_pop(L, 1);
}

void Metatable::SetEq(const std::function<bool(State& vm, Value a, Value b)>& fn)
{
    lua_State* L = table_.GetLuaState();
    if (!L) return;

    struct ClosureData { std::function<bool(State&, Value, Value)> func; };
    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        Value a = Value::FromLuaState(L, 1);
        Value b = Value::FromLuaState(L, 2);
        try
        {
            State vm(L);
            bool res = data->func(vm, a, b);
            lua_pushboolean(L, res ? 1 : 0);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ __eq callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ __eq callback threw an unknown exception");
        }
    };

    table_.PushToLuaState(L);
    auto* data = Detail::NewLuaOwnedCallbackData(L, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L, cfunc, "__eq", 1);
    lua_setfield(L, -2, "__eq");
    lua_pop(L, 1);
}

void Metatable::SetMetaMethod(const std::string& name, const Value& val)
{
    table_.Set(name, val);
}

} // namespace Lode
