// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include <string>
#include <memory>
#include <vector>
#include <variant>

struct lua_State;

namespace Lode
{

enum class ValueType
{
    Nil,
    Boolean,
    Number,
    Integer,
    String,
    Table,
    Function,
    Thread,
    Userdata,
    LightUserdata,
    Buffer
};

class State;
class Table;
class Coroutine;

class LODE_API Value
{
public:
    Value();
    Value(bool b);
    Value(double n);
    Value(int i);
    Value(const char* str);
    Value(const std::string& str);
    Value(void* lightUserdata);
    Value(const Table& table);
    Value(const Coroutine& coroutine);

    ~Value();
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;

    [[nodiscard]] ValueType GetType() const;
    [[nodiscard]] bool IsNil() const { return GetType() == ValueType::Nil; }
    [[nodiscard]] bool IsBoolean() const { return GetType() == ValueType::Boolean; }
    [[nodiscard]] bool IsNumber() const { return GetType() == ValueType::Number || GetType() == ValueType::Integer; }
    [[nodiscard]] bool IsInteger() const { return GetType() == ValueType::Integer; }
    [[nodiscard]] bool IsString() const { return GetType() == ValueType::String; }
    [[nodiscard]] bool IsTable() const { return GetType() == ValueType::Table; }
    [[nodiscard]] bool IsFunction() const { return GetType() == ValueType::Function; }
    [[nodiscard]] bool IsThread() const { return GetType() == ValueType::Thread; }
    [[nodiscard]] bool IsUserdata() const { return GetType() == ValueType::Userdata; }
    [[nodiscard]] bool IsBuffer() const { return GetType() == ValueType::Buffer; }

    [[nodiscard]] bool AsBoolean() const;
    [[nodiscard]] double AsNumber() const;
    [[nodiscard]] int AsInteger() const;
    [[nodiscard]] std::string AsString() const;
    [[nodiscard]] void* AsLightUserdata() const;
    [[nodiscard]] void* AsBuffer(size_t* sizeOut = nullptr) const;
    // Converts to a Table. Returns an empty Table if the value is not of table type.
    [[nodiscard]] Table AsTable() const;

    [[nodiscard]] Result<bool> TryAsBoolean() const;
    [[nodiscard]] Result<double> TryAsNumber() const;
    [[nodiscard]] Result<int> TryAsInteger() const;
    [[nodiscard]] Result<std::string> TryAsString() const;
    [[nodiscard]] Result<void*> TryAsBuffer(size_t* sizeOut = nullptr) const;

    // Invoke if value is a function
    Result<std::vector<Value>> Call(State& vm, const std::vector<Value>& args = {}) const;
    Result<std::vector<Value>> Call(const std::vector<Value>& args = {}) const;

    // Fast zero-allocation Call overloads (Returns single Value)
    Result<Value> CallSingle() const;
    Result<Value> CallSingle(const Value& arg1) const;
    Result<Value> CallSingle(const Value& arg1, const Value& arg2) const;
    Result<Value> CallSingle(const Value& arg1, const Value& arg2, const Value& arg3) const;


    // Internal creation for Luau stack values
    static Value FromLuaState(lua_State* L, int index);
    void PushToLuaState(lua_State* L) const;

private:
    struct RefData
    {
        lua_State* L = nullptr;
        int refId = -1;
        ~RefData();
    };

    ValueType type_ = ValueType::Nil;
    std::variant<std::monostate, bool, double, int, std::string, void*, std::shared_ptr<RefData>> data_;
};

} // namespace Lode
