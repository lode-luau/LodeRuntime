// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/State.hpp"
#include "Lode/Coroutine.hpp"
#include "LuaError.hpp"
#include "StateLifetime.hpp"
#include "lua.h"
#include "lualib.h"
#include <stdexcept>
#include <cmath>
#include <limits>

namespace Lode
{

Value::RefData::~RefData()
{
    if (L && lifetime && lifetime->alive.load() && refId != LUA_NOREF && refId != LUA_REFNIL)
    {
        lua_unref(L, refId);
    }
}

Value::Value() = default;

Value::Value(bool b) : type_(ValueType::Boolean), data_(b) {}
Value::Value(double n) : type_(ValueType::Number), data_(n) {}
Value::Value(int i) : Value(static_cast<int64_t>(i)) {}
Value::Value(int64_t i) : type_(ValueType::Integer), data_(i) {}
Value::Value(const Vector& vector) : type_(ValueType::Vector), data_(vector) {}
Value::Value(const char* str) : type_(ValueType::String), data_(std::string(str ? str : "")) {}
Value::Value(const std::string& str) : type_(ValueType::String), data_(str) {}
Value::Value(void* lightUserdata) : type_(ValueType::LightUserdata), data_(lightUserdata) {}

Value::Value(const Table& table)
{
    lua_State* L = table.GetLuaState();
    if (L)
    {
        table.PushToLuaState(L);
        type_ = ValueType::Table;
        auto ref = std::make_shared<RefData>();
        ref->L = lua_mainthread(L);
        ref->lifetime = Detail::GetStateLifetime(L);
        ref->refId = lua_ref(L, -1);
        data_ = ref;
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
        auto ref = std::make_shared<RefData>();
        ref->L = lua_mainthread(co);
        ref->thread = co;
        ref->lifetime = Detail::GetStateLifetime(co);
        ref->refId = lua_ref(co, -1);
        data_ = ref;
        lua_pop(co, 1);
    }
}

Value::Value(const Buffer& buffer)
{
    lua_State* L = buffer.GetLuaState();
    if (L && buffer.IsValid())
    {
        buffer.PushToLuaState(L);
        type_ = ValueType::Buffer;
        auto ref = std::make_shared<RefData>();
        ref->L = lua_mainthread(L);
        ref->lifetime = Detail::GetStateLifetime(L);
        ref->refId = lua_ref(L, -1);
        data_ = ref;
        lua_pop(L, 1);
    }
}

Coroutine Value::AsCoroutine() const
{
    if (type_ != ValueType::Thread) return Coroutine();
    auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_);
    if (!ref || !*ref || !(*ref)->L || !(*ref)->lifetime || !(*ref)->lifetime->alive.load()) return Coroutine();
    return Coroutine((*ref)->thread);
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

bool Value::AsBoolean() const {
    if (auto* b = std::get_if<bool>(&data_)) return *b;
    return false;
}
double Value::AsNumber() const {
    if (auto* n = std::get_if<double>(&data_)) return *n;
    if (auto* i = std::get_if<int64_t>(&data_)) return static_cast<double>(*i);
    return 0.0;
}
int64_t Value::AsInteger() const {
    if (auto* i = std::get_if<int64_t>(&data_)) return *i;
    if (auto* n = std::get_if<double>(&data_))
    {
        constexpr double int64Min = -9223372036854775808.0;
        constexpr double int64ExclusiveMax = 9223372036854775808.0;
        if (std::isfinite(*n) && *n >= int64Min && *n < int64ExclusiveMax)
            return static_cast<int64_t>(*n);
    }
    return 0;
}
Vector Value::AsVector() const
{
    if (auto* vector = std::get_if<Vector>(&data_)) return *vector;
    return Vector();
}
std::string Value::AsString() const {
    if (auto* s = std::get_if<std::string>(&data_)) return *s;
    return "";
}
void* Value::AsLightUserdata() const {
    if (auto* ptr = std::get_if<void*>(&data_)) return *ptr;
    return nullptr;
}

void* Value::AsBuffer(size_t* sizeOut) const
{
    if (type_ == ValueType::Buffer)
    {
        if (auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_))
        {
            if (*ref && (*ref)->L)
            {
                lua_getref((*ref)->L, (*ref)->refId);
                void* ptr = lua_tobuffer((*ref)->L, -1, sizeOut);
                lua_pop((*ref)->L, 1);
                return ptr;
            }
        }
    }
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}

std::span<uint8_t> Value::AsSpan() const
{
    size_t size = 0;
    void* ptr = AsBuffer(&size);
    if (ptr) return std::span<uint8_t>(static_cast<uint8_t*>(ptr), size);
    return std::span<uint8_t>();
}

Table Value::AsTable() const
{
    if (type_ == ValueType::Table)
    {
        if (auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_))
        {
            if (*ref && (*ref)->L)
            {
                lua_State* L = (*ref)->L;
                lua_getref(L, (*ref)->refId);
                Table t(L, -1);
                lua_pop(L, 1);
                return t;
            }
        }
    }
    return Table();
}

Buffer Value::AsBufferObj() const
{
    if (type_ == ValueType::Buffer)
    {
        if (auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_))
        {
            if (*ref && (*ref)->L)
            {
                lua_State* L = (*ref)->L;
                lua_getref(L, (*ref)->refId);
                Buffer b(L, -1);
                lua_pop(L, 1);
                return b;
            }
        }
    }
    return Buffer();
}

Result<bool> Value::TryAsBoolean() const
{
    if (auto* b = std::get_if<bool>(&data_)) return *b;
    return Error::Type("Value is not a boolean");
}

Result<double> Value::TryAsNumber() const
{
    if (auto* n = std::get_if<double>(&data_)) return *n;
    if (auto* i = std::get_if<int64_t>(&data_)) return static_cast<double>(*i);
    return Error::Type("Value is not a number");
}

Result<int64_t> Value::TryAsInteger() const
{
    if (auto* i = std::get_if<int64_t>(&data_)) return *i;
    if (auto* n = std::get_if<double>(&data_))
    {
        constexpr double int64Min = -9223372036854775808.0;
        constexpr double int64ExclusiveMax = 9223372036854775808.0;
        if (std::isfinite(*n) && *n == std::trunc(*n) &&
            *n >= int64Min && *n < int64ExclusiveMax)
            return static_cast<int64_t>(*n);
        return Error::Type("Number is outside the integer range");
    }
    return Error::Type("Value is not an integer");
}

Result<Vector> Value::TryAsVector() const
{
    if (auto* vector = std::get_if<Vector>(&data_)) return *vector;
    return Error::Type("Value is not a vector");
}

Result<std::string> Value::TryAsString() const
{
    if (auto* s = std::get_if<std::string>(&data_)) return *s;
    return Error::Type("Value is not a string");
}

Result<void*> Value::TryAsBuffer(size_t* sizeOut) const
{
    if (type_ == ValueType::Buffer) return AsBuffer(sizeOut);
    return Error::Type("Value is not a buffer");
}

Result<Buffer> Value::TryAsBufferObj() const
{
    if (type_ == ValueType::Buffer) return AsBufferObj();
    return Error::Type("Value is not a buffer");
}

namespace
{
    // Shared multi-return call core: pushes the function and its arguments, runs
    // lua_pcall with LUA_MULTRET, and collects every returned Value.
    Result<std::vector<Value>> CallMultiReturn(lua_State* L, const Value& fn, std::span<const Value> args)
    {
        if (!L || fn.GetType() != ValueType::Function)
        {
            return Error::Type("Value is not callable");
        }

        // Record the stack top before pushing anything so we can calculate how many
        // values the function actually returned after lua_pcall with LUA_MULTRET.
        int topBefore = lua_gettop(L);

        fn.PushToLuaState(L);
        for (const auto& arg : args)
        {
            arg.PushToLuaState(L);
        }

        int status = lua_pcall(L, static_cast<int>(args.size()), LUA_MULTRET, 0);
        if (status != LUA_OK)
        {
            std::string errStr = LuaErrorMessage(L, -1);
            lua_pop(L, 1);
            return Error::Runtime("Function execution failed: " + errStr);
        }

        // Collect only the values that the function pushed onto the stack.
        int nresults = lua_gettop(L) - topBefore;
        std::vector<Value> results;
        results.reserve(nresults);
        for (int i = topBefore + 1; i <= topBefore + nresults; ++i)
        {
            results.push_back(Value::FromLuaState(L, i));
        }
        lua_pop(L, nresults);
        return results;
    }

    // Shared single-return call core: pushes the function and its arguments, runs
    // lua_pcall expecting one result, and returns the first returned Value.
    Result<Value> CallSingleReturn(lua_State* L, const Value& fn, std::span<const Value> args)
    {
        if (!L || fn.GetType() != ValueType::Function)
        {
            return Error::Type("Value is not callable");
        }

        fn.PushToLuaState(L);
        for (const auto& arg : args)
        {
            arg.PushToLuaState(L);
        }

        int status = lua_pcall(L, static_cast<int>(args.size()), 1, 0);
        if (status != LUA_OK)
        {
            std::string errStr = LuaErrorMessage(L, -1);
            lua_pop(L, 1);
            return Error::Runtime("Function execution failed: " + errStr);
        }
        Value result = Value::FromLuaState(L, -1);
        lua_pop(L, 1);
        return result;
    }
} // namespace

lua_State* Value::GetCapturedState() const
{
    if (auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_))
    {
        if (*ref) return (*ref)->L;
    }
    return nullptr;
}

Result<std::vector<Value>> Value::CallArgs(State& vm, Detail::SmallValueList args) const
{
    return CallMultiReturn(vm.GetLuaState(), *this, args.AsSpan());
}

Result<std::vector<Value>> Value::CallArgs(Detail::SmallValueList args) const
{
    return CallMultiReturn(GetCapturedState(), *this, args.AsSpan());
}

Result<Value> Value::CallSingleArgs(State& vm, Detail::SmallValueList args) const
{
    return CallSingleReturn(vm.GetLuaState(), *this, args.AsSpan());
}

Result<Value> Value::CallSingleArgs(Detail::SmallValueList args) const
{
    return CallSingleReturn(GetCapturedState(), *this, args.AsSpan());
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
        val.data_ = (lua_toboolean(L, index) != 0);
        break;
    case LUA_TNUMBER:
        val.type_ = ValueType::Number;
        val.data_ = lua_tonumber(L, index);
        break;
    case LUA_TINTEGER:
        val.type_ = ValueType::Integer;
        val.data_ = lua_tointeger64(L, index, nullptr);
        break;
    case LUA_TVECTOR:
        val.type_ = ValueType::Vector;
        {
            const float* components = lua_tovector(L, index);
            Vector vector;
            vector.size = LUA_VECTOR_SIZE;
            if (components)
            {
                for (size_t i = 0; i < vector.size; ++i)
                    vector.components[i] = components[i];
            }
            val.data_ = vector;
        }
        break;
    case LUA_TSTRING:
        val.type_ = ValueType::String;
        {
            size_t length = 0;
            const char* str = lua_tolstring(L, index, &length);
            val.data_ = str ? std::string(str, length) : std::string();
        }
        break;
    case LUA_TLIGHTUSERDATA:
        val.type_ = ValueType::LightUserdata;
        val.data_ = lua_touserdata(L, index);
        break;
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TTHREAD:
    case LUA_TUSERDATA:
    case LUA_TBUFFER:
    {
        val.type_ = (type == LUA_TTABLE) ? ValueType::Table :
                    (type == LUA_TFUNCTION) ? ValueType::Function :
                    (type == LUA_TTHREAD) ? ValueType::Thread : 
                    (type == LUA_TBUFFER) ? ValueType::Buffer : ValueType::Userdata;
        auto ref = std::make_shared<RefData>();
        ref->L = lua_mainthread(L);
        ref->lifetime = Detail::GetStateLifetime(L);
        if (type == LUA_TTHREAD) ref->thread = lua_tothread(L, index);
        lua_pushvalue(L, index);
        ref->refId = lua_ref(L, -1);
        lua_pop(L, 1);
        val.data_ = ref;
        break;
    }
    default:
        val.type_ = ValueType::Nil;
        break;
    }
    return val;
}

void Value::PushToLuaState(lua_State* L) const
{
    if (!L)
        return;

    switch (type_)
    {
    case ValueType::Nil:
        lua_pushnil(L);
        break;
    case ValueType::Boolean:
        if (auto* b = std::get_if<bool>(&data_)) lua_pushboolean(L, *b ? 1 : 0);
        else lua_pushboolean(L, 0);
        break;
    case ValueType::Number:
        if (auto* n = std::get_if<double>(&data_)) lua_pushnumber(L, *n);
        else lua_pushnumber(L, 0.0);
        break;
    case ValueType::Integer:
        if (auto* i = std::get_if<int64_t>(&data_)) lua_pushinteger64(L, *i);
        else lua_pushinteger(L, 0);
        break;
    case ValueType::Vector:
        if (auto* vector = std::get_if<Vector>(&data_))
        {
#if LUA_VECTOR_SIZE == 4
            lua_pushvector(L, vector->components[0], vector->components[1], vector->components[2], vector->components[3]);
#else
            lua_pushvector(L, vector->components[0], vector->components[1], vector->components[2]);
#endif
        }
        else
        {
#if LUA_VECTOR_SIZE == 4
            lua_pushvector(L, 0.0f, 0.0f, 0.0f, 0.0f);
#else
            lua_pushvector(L, 0.0f, 0.0f, 0.0f);
#endif
        }
        break;
    case ValueType::String:
        if (auto* s = std::get_if<std::string>(&data_)) lua_pushlstring(L, s->data(), s->size());
        else lua_pushstring(L, "");
        break;
    case ValueType::LightUserdata:
        if (auto* ptr = std::get_if<void*>(&data_)) lua_pushlightuserdata(L, *ptr);
        else lua_pushlightuserdata(L, nullptr);
        break;
    case ValueType::Table:
    case ValueType::Function:
    case ValueType::Thread:
    case ValueType::Userdata:
    case ValueType::Buffer:
        if (auto* ref = std::get_if<std::shared_ptr<RefData>>(&data_))
        {
            if (*ref && (*ref)->L && (*ref)->refId != LUA_NOREF && (*ref)->refId != LUA_REFNIL)
            {
                // References are resolved on their owning main thread. A value can
                // then be moved to another thread in the same VM, but never to an
                // unrelated lua_State.
                if (lua_mainthread(L) != (*ref)->L)
                {
                    lua_pushnil(L);
                    break;
                }

                lua_getref((*ref)->L, (*ref)->refId);
                if (L != (*ref)->L)
                    lua_xmove((*ref)->L, L, 1);
            }
            else
            {
                lua_pushnil(L);
            }
        }
        else
        {
            lua_pushnil(L);
        }
        break;
    }
}

} // namespace Lode
