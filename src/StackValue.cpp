#include "Lode/StackValue.hpp"
#include "lua.h"

namespace Lode
{

StackValue::StackValue(lua_State* L, int index) : L_(L), index_(index) {}

ValueType StackValue::GetType() const
{
    int type = lua_type(L_, index_);
    switch (type)
    {
    case LUA_TNIL: return ValueType::Nil;
    case LUA_TBOOLEAN: return ValueType::Boolean;
    case LUA_TNUMBER: return ValueType::Number; // Could be Integer, but Lua API just returns Number
    case LUA_TSTRING: return ValueType::String;
    case LUA_TTABLE: return ValueType::Table;
    case LUA_TFUNCTION: return ValueType::Function;
    case LUA_TTHREAD: return ValueType::Thread;
    case LUA_TUSERDATA: return ValueType::Userdata;
    case LUA_TLIGHTUSERDATA: return ValueType::LightUserdata;
    case LUA_TBUFFER: return ValueType::Buffer;
    default: return ValueType::Nil;
    }
}

bool StackValue::IsNil() const { return lua_isnil(L_, index_); }
bool StackValue::IsBoolean() const { return lua_isboolean(L_, index_); }
bool StackValue::IsNumber() const { return lua_isnumber(L_, index_); }
bool StackValue::IsInteger() const { return lua_isnumber(L_, index_); }
bool StackValue::IsString() const { return lua_isstring(L_, index_); }
bool StackValue::IsBuffer() const { return lua_type(L_, index_) == LUA_TBUFFER; }

bool StackValue::AsBoolean() const
{
    return lua_toboolean(L_, index_) != 0;
}

double StackValue::AsNumber() const
{
    return lua_tonumber(L_, index_);
}

int StackValue::AsInteger() const
{
    return static_cast<int>(lua_tointeger(L_, index_));
}

std::string StackValue::AsString() const
{
    return lua_tostring(L_, index_);
}

void* StackValue::AsBuffer(size_t* sizeOut) const
{
    if (lua_type(L_, index_) == LUA_TBUFFER)
    {
        return lua_tobuffer(L_, index_, sizeOut);
    }
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}

Result<double> StackValue::TryAsNumber() const
{
    if (IsNumber()) return lua_tonumber(L_, index_);
    return Error::Type("Value is not a number");
}

Value StackValue::ToValue() const
{
    return Value::FromLuaState(L_, index_);
}


StackArgs::StackArgs(lua_State* L) : L_(L), numArgs_(lua_gettop(L)) {}

size_t StackArgs::Size() const
{
    return numArgs_;
}

StackValue StackArgs::operator[](size_t i) const
{
    // Lua stack starts at 1, but C++ APIs usually start at 0
    return StackValue(L_, static_cast<int>(i) + 1);
}

} // namespace Lode
