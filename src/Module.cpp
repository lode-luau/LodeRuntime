// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"
#include "NativeCallback.hpp"
#include "LuaError.hpp"
#include "lua.h"
#include "lualib.h"
#include <vector>

namespace Lode
{

Exports::Exports(State& vm) : L_(vm.GetLuaState()), exportsTable_()
{
    exportsTable_ = vm.CreateTable();
}

Exports::Exports(lua_State* L) : L_(L), exportsTable_()
{
    State vm(L);
    exportsTable_ = vm.CreateTable();
}

void Exports::Function(const std::string& name, const std::function<Value(State& vm, const std::vector<Value>& args)>& fn)
{
    struct ClosureData
    {
        std::function<Value(State& vm, const std::vector<Value>& args)> func;
    };

    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        int top = lua_gettop(L);
        std::vector<Value> args;
        args.reserve(top);
        for (int i = 1; i <= top; ++i)
        {
            args.push_back(Value::FromLuaState(L, i));
        }

        try
        {
            State vm(L);
            Value result = data->func(vm, args);
            if (lua_status(L) == LUA_YIELD)
                return lua_yield(L, 0);
            result.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& error)
        {
            return RaiseCppException(L, "C++ native callback exception", error);
        }
        catch (...)
        {
            return RaiseCppException(L, "C++ native callback threw an unknown exception");
        }
    };

    exportsTable_.PushToLuaState(L_);
    auto* data = Detail::NewLuaOwnedCallbackData(L_, ClosureData{ fn });
    (void)data;
    lua_pushcclosure(L_, cfunc, name.c_str(), 1);
    lua_setfield(L_, -2, name.c_str());
    lua_pop(L_, 1);
}

void Exports::Function(const std::string& name, const std::function<Value()>& fn)
{
    Function(name, [fn](State&, const std::vector<Value>&) -> Value {
        return fn();
    });
}

void Exports::Function(const std::string& name, const std::function<std::string(const std::string&)>& fn)
{
    Function(name, [fn](State&, const std::vector<Value>& args) -> Value {
        std::string argStr = (!args.empty() && args[0].IsString()) ? args[0].AsString() : "";
        return Value(fn(argStr));
    });
}

void Exports::Function(const std::string& name, const std::function<double(double)>& fn)
{
    Function(name, [fn](State&, const std::vector<Value>& args) -> Value {
        double argNum = (!args.empty() && args[0].IsNumber()) ? args[0].AsNumber() : 0.0;
        return Value(fn(argNum));
    });
}

void Exports::SetTable(const std::string& name, const Lode::Table& table)
{
    exportsTable_.Set(name, Value());
    exportsTable_.PushToLuaState(L_);
    table.PushToLuaState(L_);
    lua_setfield(L_, -2, name.c_str());
    lua_pop(L_, 1);
}

void Exports::SetValue(const std::string& name, const Lode::Value& value)
{
    exportsTable_.Set(name, value);
}

} // namespace Lode
