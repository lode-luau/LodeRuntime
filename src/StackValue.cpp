#include "Lode/StackValue.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Table.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/Coroutine.hpp"
#include "lua.h"

#include <cstdint>

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
    case LUA_TINTEGER: return ValueType::Integer;
    case LUA_TVECTOR: return ValueType::Vector;
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
bool StackValue::IsVector() const { return lua_isvector(L_, index_); }
bool StackValue::IsInteger() const
{
    if (lua_type(L_, index_) == LUA_TINTEGER) return true;
    if (!lua_isnumber(L_, index_)) return false;
    return Numeric::ToInt64(lua_tonumber(L_, index_), "value").IsOk();
}
bool StackValue::IsString() const { return lua_isstring(L_, index_); }
bool StackValue::IsBuffer() const { return lua_type(L_, index_) == LUA_TBUFFER; }
bool StackValue::IsTable() const { return lua_istable(L_, index_); }
bool StackValue::IsFunction() const { return lua_isfunction(L_, index_); }
bool StackValue::IsThread() const { return lua_isthread(L_, index_); }
bool StackValue::IsUserdata() const { return lua_isuserdata(L_, index_); }

bool StackValue::AsBoolean() const
{
    return lua_toboolean(L_, index_) != 0;
}

double StackValue::AsNumber() const
{
    return lua_tonumber(L_, index_);
}

int64_t StackValue::AsInteger() const
{
    if (lua_type(L_, index_) == LUA_TINTEGER)
        return lua_tointeger64(L_, index_, nullptr);

    auto result = Numeric::ToInt64(lua_tonumber(L_, index_), "value");
    return result.IsError() ? 0 : result.GetValue();
}

Vector StackValue::AsVector() const
{
    Vector vector;
    const float* components = lua_tovector(L_, index_);
    vector.size = LUA_VECTOR_SIZE;
    if (components)
    {
        for (size_t i = 0; i < vector.size; ++i)
            vector.components[i] = components[i];
    }
    return vector;
}

std::string StackValue::AsString() const
{
    size_t length = 0;
    const char* str = lua_tolstring(L_, index_, &length);
    return str ? std::string(str, length) : std::string();
}

std::string_view StackValue::AsStringView() const
{
    size_t len = 0;
    const char* str = lua_tolstring(L_, index_, &len);
    return str ? std::string_view(str, len) : std::string_view();
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

std::span<uint8_t> StackValue::AsSpan() const
{
    size_t size = 0;
    void* ptr = AsBuffer(&size);
    if (ptr) return std::span<uint8_t>(static_cast<uint8_t*>(ptr), size);
    return std::span<uint8_t>();
}

Result<double> StackValue::TryAsNumber() const
{
    if (IsNumber()) return lua_tonumber(L_, index_);
    return Error::Type("Value is not a number");
}

bool StackValue::IsLightUserdata() const { return lua_type(L_, index_) == LUA_TLIGHTUSERDATA; }

void* StackValue::AsLightUserdata() const { return lua_touserdata(L_, index_); }

Result<bool> StackValue::TryAsBoolean() const
{
    if (IsBoolean()) return AsBoolean();
    return Error::Type("Value is not a boolean");
}

Result<int64_t> StackValue::TryAsInteger() const
{
    if (IsInteger()) return AsInteger();
    return Error::Type("Value is not an integer");
}

Result<std::string> StackValue::TryAsString() const
{
    if (IsString()) return AsString();
    return Error::Type("Value is not a string");
}

Buffer StackValue::AsBufferObj() const
{
    return ToValue().AsBufferObj();
}

Coroutine StackValue::AsCoroutine() const
{
    return ToValue().AsCoroutine();
}

Table StackValue::AsTable() const
{
    return ToValue().AsTable();
}

Value StackValue::ToValue() const
{
    return Value::FromLuaState(L_, index_);
}


StackArgs::StackArgs(lua_State* L) : L_(L), numArgs_(lua_gettop(L)) {}

size_t StackArgs::Size() const
{
    return numArgs_ < begin_ ? 0 : static_cast<size_t>(numArgs_ - begin_ + 1);
}

std::vector<Value> StackArgs::ToVector() const
{
    std::vector<Value> vec;
    vec.reserve(Size());
    for (int i = begin_; i <= numArgs_; ++i)
    {
        vec.push_back(Value::FromLuaState(L_, i));
    }
    return vec;
}

StackValue StackArgs::operator[](size_t i) const
{
    // Lua stack starts at 1, but C++ APIs usually start at 0.
    // begin_ supports sliced views (e.g. dropping the self argument).
    return StackValue(L_, begin_ + static_cast<int>(i));
}

} // namespace Lode
