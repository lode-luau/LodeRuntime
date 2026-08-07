// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/State.hpp"
#include "Lode/Coroutine.hpp"
#include "LuaError.hpp"
#include "StateLifetime.hpp"
#include "PinnedRef.hpp"
#include "lua.h"
#include "lualib.h"
#include "lstate.h"
#include <stdexcept>
#include <cmath>

namespace Lode
{

namespace
{
    // Captures the value at the top of the stack into a pinned reference and
    // removes it from the stack.
    std::shared_ptr<Detail::PinnedRef> PinStackTop(lua_State* L)
    {
        auto ref = std::make_shared<Detail::PinnedRef>(Detail::CaptureRef(L, -1));
        lua_pop(L, 1);
        return ref;
    }

    // Pushes the referenced value onto its owning state and returns that state.
    // Returns nullptr (pushing nothing) when the reference is empty.
    lua_State* PushValueRef(const std::shared_ptr<Detail::PinnedRef>& ref)
    {
        if (!ref || !ref->L) return nullptr;
        Detail::PushRef(ref->L, *ref);
        return ref->L;
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
        data_ = PinStackTop(L);
    }
}

Value::Value(const Coroutine& coroutine)
{
    lua_State* co = coroutine.GetThreadState();
    if (co)
    {
        if ((co->status == LUA_YIELD || co->status == LUA_BREAK) && co->top + 1 > co->ci->top)
        {
            // Luau freezes the innermost frame limit (ci->top) to the stack
            // position at the yield point (see ldo.cpp resume_finish). A
            // suspended thread whose frame is exactly full trips the
            // api_check in lua_pushthread's api_incr_top, so relax the
            // frozen limit to make room for the push.
            co->ci->top = co->top + 1;
        }
        lua_pushthread(co);
        type_ = ValueType::Thread;
        data_ = PinStackTop(co);
    }
}

Value::Value(const Buffer& buffer)
{
    lua_State* L = buffer.GetLuaState();
    if (L && buffer.IsValid())
    {
        buffer.PushToLuaState(L);
        type_ = ValueType::Buffer;
        data_ = PinStackTop(L);
    }
}

Coroutine Value::AsCoroutine() const
{
    if (type_ != ValueType::Thread) return Coroutine();
    auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_);
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
        if (auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_))
        {
            if (lua_State* L = PushValueRef(*ref))
            {
                void* ptr = lua_tobuffer(L, -1, sizeOut);
                lua_pop(L, 1);
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
        if (auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_))
        {
            if (lua_State* L = PushValueRef(*ref))
            {
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
        if (auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_))
        {
            if (lua_State* L = PushValueRef(*ref))
            {
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
    // Shared call core: pushes the function and its arguments, runs lua_pcall
    // requesting `nresults`, and collects every returned Value.
    Result<std::vector<Value>> CallCore(lua_State* L, const Value& fn, std::span<const Value> args, int nresults)
    {
        if (!L)
        {
            return Error::Runtime("Cannot call a value without a Lua state");
        }

        int topBefore = lua_gettop(L);

        fn.PushToLuaState(L);
        for (const auto& arg : args)
        {
            arg.PushToLuaState(L);
        }

        int status = lua_pcall(L, static_cast<int>(args.size()), nresults, 0);
        if (status != LUA_OK)
        {
            std::string errStr = LuaErrorMessage(L, -1);
            lua_pop(L, 1);
            return Error::Runtime("Function execution failed: " + errStr);
        }

        int nresultsActual = lua_gettop(L) - topBefore;
        std::vector<Value> results;
        results.reserve(nresultsActual);
        for (int i = topBefore + 1; i <= topBefore + nresultsActual; ++i)
        {
            results.push_back(Value::FromLuaState(L, i));
        }
        lua_pop(L, nresultsActual);
        return results;
    }

    Result<Value> CallSingleCore(lua_State* L, const Value& fn, std::span<const Value> args)
    {
        Result<std::vector<Value>> res = CallCore(L, fn, args, 1);
        if (res.IsError()) return res.GetError();
        const auto& values = res.GetValue();
        return values.empty() ? Result<Value>(Value()) : Result<Value>(values.front());
    }
} // namespace

lua_State* Value::GetCapturedState() const
{
    if (auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_))
    {
        if (*ref) return (*ref)->L;
    }
    return nullptr;
}

Result<std::vector<Value>> Value::CallArgs(State& vm, Detail::SmallValueList args) const
{
    return CallCore(vm.GetLuaState(), *this, args.AsSpan(), LUA_MULTRET);
}

Result<std::vector<Value>> Value::CallArgs(Detail::SmallValueList args) const
{
    return CallCore(GetCapturedState(), *this, args.AsSpan(), LUA_MULTRET);
}

Result<Value> Value::CallSingleArgs(State& vm, Detail::SmallValueList args) const
{
    return CallSingleCore(vm.GetLuaState(), *this, args.AsSpan());
}

Result<Value> Value::CallSingleArgs(Detail::SmallValueList args) const
{
    return CallSingleCore(GetCapturedState(), *this, args.AsSpan());
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
        val.data_ = std::make_shared<Detail::PinnedRef>(Detail::CaptureRef(L, index));
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
        if (auto* ref = std::get_if<std::shared_ptr<Detail::PinnedRef>>(&data_))
        {
            if (*ref)
                Detail::PushRef(L, **ref);
            else
                lua_pushnil(L);
        }
        else
        {
            lua_pushnil(L);
        }
        break;
    }
}

} // namespace Lode
