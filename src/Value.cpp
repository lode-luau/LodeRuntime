// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/State.hpp"
#include "Lode/Coroutine.hpp"
#include "lua.h"
#include "lualib.h"
#include <stdexcept>

namespace Lode
{

Value::RefData::~RefData()
{
    if (L && refId != LUA_NOREF && refId != LUA_REFNIL)
    {
        lua_unref(L, refId);
    }
}

Value::Value() = default;

Value::Value(bool b) : type_(ValueType::Boolean), boolVal_(b) {}
Value::Value(double n) : type_(ValueType::Number), numberVal_(n) {}
Value::Value(int i) : type_(ValueType::Integer), intVal_(i), numberVal_(static_cast<double>(i)) {}
Value::Value(const char* str) : type_(ValueType::String), stringVal_(str ? str : "") {}
Value::Value(const std::string& str) : type_(ValueType::String), stringVal_(str) {}
Value::Value(void* lightUserdata) : type_(ValueType::LightUserdata), lightUserdataVal_(lightUserdata) {}

Value::Value(const Table& table)
{
    lua_State* L = table.GetLuaState();
    if (L)
    {
        table.PushToLuaState(L);
        type_ = ValueType::Table;
        refData_ = std::make_shared<RefData>();
        refData_->L = L;
        refData_->refId = lua_ref(L, -1);
        lua_pop(L, 1);
    }
}

Value::Value(const Coroutine& coroutine)
{
    lua_State* co = coroutine.GetThreadState();
    if (co)
    {
        lua_pushthread(co);
        type_ = ValueType::Thread;
        refData_ = std::make_shared<RefData>();
        refData_->L = co;
        refData_->refId = lua_ref(co, -1);
        lua_pop(co, 1);
    }
}

Value::~Value() = default;
Value::Value(const Value& other) = default;
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(const Value& other) = default;
Value& Value::operator=(Value&& other) noexcept = default;

ValueType Value::GetType() const
{
    return type_;
}

bool Value::AsBoolean() const { return boolVal_; }
double Value::AsNumber() const { return numberVal_; }
int Value::AsInteger() const { return intVal_; }
std::string Value::AsString() const { return stringVal_; }
void* Value::AsLightUserdata() const { return lightUserdataVal_; }

Result<bool> Value::TryAsBoolean() const
{
    if (type_ == ValueType::Boolean) return boolVal_;
    return Error::Type("Value is not a boolean");
}

Result<double> Value::TryAsNumber() const
{
    if (type_ == ValueType::Number || type_ == ValueType::Integer) return numberVal_;
    return Error::Type("Value is not a number");
}

Result<int> Value::TryAsInteger() const
{
    if (type_ == ValueType::Integer) return intVal_;
    if (type_ == ValueType::Number) return static_cast<int>(numberVal_);
    return Error::Type("Value is not an integer");
}

Result<std::string> Value::TryAsString() const
{
    if (type_ == ValueType::String) return stringVal_;
    return Error::Type("Value is not a string");
}

Result<std::vector<Value>> Value::Call(State& vm, const std::vector<Value>& args) const
{
    lua_State* L = vm.GetLuaState();
    if (!L || type_ != ValueType::Function)
    {
        return Error::Type("Value is not callable");
    }

    PushToLuaState(L);
    for (const auto& arg : args)
    {
        arg.PushToLuaState(L);
    }

    int status = lua_pcall(L, static_cast<int>(args.size()), LUA_MULTRET, 0);
    if (status != LUA_OK)
    {
        std::string errStr = lua_tostring(L, -1);
        lua_pop(L, 1);
        return Error::Runtime("Function execution failed: " + errStr);
    }

    int top = lua_gettop(L);
    std::vector<Value> results;
    results.reserve(top);
    for (int i = 1; i <= top; ++i)
    {
        results.push_back(Value::FromLuaState(L, i));
    }
    lua_pop(L, top);
    return results;
}

Value Value::FromLuaState(lua_State* L, int index)
{
    Value val;
    int type = lua_type(L, index);
    switch (type)
    {
    case LUA_TNIL:
        val.type_ = ValueType::Nil;
        break;
    case LUA_TBOOLEAN:
        val.type_ = ValueType::Boolean;
        val.boolVal_ = (lua_toboolean(L, index) != 0);
        break;
    case LUA_TNUMBER:
        val.type_ = ValueType::Number;
        val.numberVal_ = lua_tonumber(L, index);
        val.intVal_ = static_cast<int>(val.numberVal_);
        break;
    case LUA_TSTRING:
        val.type_ = ValueType::String;
        val.stringVal_ = lua_tostring(L, index);
        break;
    case LUA_TLIGHTUSERDATA:
        val.type_ = ValueType::LightUserdata;
        val.lightUserdataVal_ = lua_touserdata(L, index);
        break;
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TTHREAD:
    case LUA_TUSERDATA:
        val.type_ = (type == LUA_TTABLE) ? ValueType::Table :
                    (type == LUA_TFUNCTION) ? ValueType::Function :
                    (type == LUA_TTHREAD) ? ValueType::Thread : ValueType::Userdata;
        val.refData_ = std::make_shared<RefData>();
        val.refData_->L = L;
        lua_pushvalue(L, index);
        val.refData_->refId = lua_ref(L, -1);
        lua_pop(L, 1);
        break;
    default:
        val.type_ = ValueType::Nil;
        break;
    }
    return val;
}

void Value::PushToLuaState(lua_State* L) const
{
    switch (type_)
    {
    case ValueType::Nil:
        lua_pushnil(L);
        break;
    case ValueType::Boolean:
        lua_pushboolean(L, boolVal_ ? 1 : 0);
        break;
    case ValueType::Number:
        lua_pushnumber(L, numberVal_);
        break;
    case ValueType::Integer:
        lua_pushinteger(L, intVal_);
        break;
    case ValueType::String:
        lua_pushlstring(L, stringVal_.data(), stringVal_.size());
        break;
    case ValueType::LightUserdata:
        lua_pushlightuserdata(L, lightUserdataVal_);
        break;
    case ValueType::Table:
    case ValueType::Function:
    case ValueType::Thread:
    case ValueType::Userdata:
        if (refData_ && refData_->refId != LUA_NOREF)
        {
            lua_getref(L, refData_->refId);
        }
        else
        {
            lua_pushnil(L);
        }
        break;
    }
}

} // namespace Lode
