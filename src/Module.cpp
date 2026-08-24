// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Module.hpp"
#include "lua.h"
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
    State vm(L_);
    exportsTable_.Set(name, vm.CreateFunction(fn));
}

void Exports::Function(const std::string& name, const std::function<Value(State& vm, StackArgs args)>& fn)
{
    State vm(L_);
    exportsTable_.Set(name, vm.CreateFastFunction(fn));
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
    exportsTable_.Set(name, table);
}

void Exports::SetValue(const std::string& name, const Lode::Value& value)
{
    exportsTable_.Set(name, value);
}

} // namespace Lode
