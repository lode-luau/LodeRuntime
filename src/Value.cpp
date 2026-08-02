#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
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

Value::Value() : type_(ValueType::Nil) {}
Value::Value(bool b) : type_(ValueType::Boolean), boolVal_(b) {}
Value::Value(double n) : type_(ValueType::Number), numberVal_(n) {}
Value::Value(int i) : type_(ValueType::Integer), intVal_(i), numberVal_(i) {}
Value::Value(const char* str) : type_(ValueType::String), stringVal_(str ? str : "") {}
Value::Value(const std::string& str) : type_(ValueType::String), stringVal_(str) {}
Value::Value(void* lightUserdata) : type_(ValueType::LightUserdata), lightUserdataVal_(lightUserdata) {}

Value::Value(const Table& table) : type_(ValueType::Table)
{
    lua_State* L = table.GetLuaState();
    if (L)
    {
        table.PushToLuaState(L);
        refData_ = std::make_shared<RefData>();
        refData_->L = L;
        refData_->refId = lua_ref(L, -1);
        lua_pop(L, 1);
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

bool Value::AsBoolean() const
{
    if (IsBoolean()) return boolVal_;
    if (IsNil()) return false;
    return true;
}

double Value::AsNumber() const
{
    if (IsNumber()) return numberVal_;
    if (IsInteger()) return static_cast<double>(intVal_);
    throw std::runtime_error("Value is not a Number");
}

int Value::AsInteger() const
{
    if (IsInteger()) return intVal_;
    if (IsNumber()) return static_cast<int>(numberVal_);
    throw std::runtime_error("Value is not an Integer");
}

std::string Value::AsString() const
{
    if (IsString()) return stringVal_;
    throw std::runtime_error("Value is not a String");
}

void* Value::AsLightUserdata() const
{
    if (GetType() == ValueType::LightUserdata) return lightUserdataVal_;
    throw std::runtime_error("Value is not LightUserdata");
}

Result<bool> Value::TryAsBoolean() const
{
    return AsBoolean();
}

Result<double> Value::TryAsNumber() const
{
    if (IsNumber()) return AsNumber();
    return Error::Type("Value is not a number");
}

Result<int> Value::TryAsInteger() const
{
    if (IsNumber()) return AsInteger();
    return Error::Type("Value is not an integer");
}

Result<std::string> Value::TryAsString() const
{
    if (IsString()) return AsString();
    return Error::Type("Value is not a string");
}

Value Value::FromLuaState(lua_State* L, int index)
{
    int t = lua_type(L, index);
    switch (t)
    {
    case LUA_TNIL:
        return Value();
    case LUA_TBOOLEAN:
        return Value(static_cast<bool>(lua_toboolean(L, index)));
    case LUA_TNUMBER:
        return Value(lua_tonumber(L, index));
    case LUA_TSTRING:
        return Value(lua_tostring(L, index));
    case LUA_TLIGHTUSERDATA:
        return Value(lua_touserdata(L, index));
    default:
    {
        Value val;
        val.refData_ = std::make_shared<RefData>();
        val.refData_->L = L;
        val.refData_->refId = lua_ref(L, index);

        if (t == LUA_TTABLE) val.type_ = ValueType::Table;
        else if (t == LUA_TFUNCTION) val.type_ = ValueType::Function;
        else if (t == LUA_TTHREAD) val.type_ = ValueType::Thread;
        else if (t == LUA_TUSERDATA) val.type_ = ValueType::Userdata;
        else val.type_ = ValueType::Nil;

        return val;
    }
    }
}

void Value::PushToLuaState(lua_State* L) const
{
    switch (type_)
    {
    case ValueType::Nil:
        lua_pushnil(L);
        break;
    case ValueType::Boolean:
        lua_pushboolean(L, boolVal_);
        break;
    case ValueType::Number:
        lua_pushnumber(L, numberVal_);
        break;
    case ValueType::Integer:
        lua_pushinteger(L, intVal_);
        break;
    case ValueType::String:
        lua_pushlstring(L, stringVal_.data(), stringVal_.length());
        break;
    case ValueType::LightUserdata:
        lua_pushlightuserdata(L, lightUserdataVal_);
        break;
    default:
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
