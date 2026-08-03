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
#include <span>
#include <cstdint>

struct lua_State;

namespace Lode
{

/**
 * @brief Represents all possible types of a Luau value.
 */
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
class Buffer;

/**
 * @brief Represents a generic Luau value that can hold any primitive or reference type.
 * 
 * Lode::Value manages the lifecycle of Luau objects. When holding a reference type
 * (like Table, Function, or Buffer), it uses a std::shared_ptr to pin the object
 * in the Lua Garbage Collector.
 */
class LODE_API Value
{
public:
    /** @brief Constructs a Nil value. */
    Value();
    /** @brief Constructs a Boolean value. */
    Value(bool b);
    /** @brief Constructs a Number value. */
    Value(double n);
    /** @brief Constructs an Integer value. */
    Value(int i);
    /** @brief Constructs a String value from a C-string. */
    Value(const char* str);
    /** @brief Constructs a String value from a std::string. */
    Value(const std::string& str);
    /** @brief Constructs a LightUserdata value. */
    Value(void* lightUserdata);
    /** @brief Constructs a Value wrapping a Table. */
    Value(const Table& table);
    /** @brief Constructs a Value wrapping a Coroutine. */
    Value(const Coroutine& coroutine);
    /** @brief Constructs a Value wrapping a Buffer. */
    Value(const Buffer& buffer);

    ~Value();
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;

    [[nodiscard]] ValueType GetType() const;
    /** @brief Checks if the value is Nil. */
    [[nodiscard]] bool IsNil() const { return GetType() == ValueType::Nil; }
    /** @brief Checks if the value is a Boolean. */
    [[nodiscard]] bool IsBoolean() const { return GetType() == ValueType::Boolean; }
    /** @brief Checks if the value is a Number (or Integer). */
    [[nodiscard]] bool IsNumber() const { return GetType() == ValueType::Number || GetType() == ValueType::Integer; }
    /** @brief Checks if the value is an Integer. */
    [[nodiscard]] bool IsInteger() const { return GetType() == ValueType::Integer; }
    /** @brief Checks if the value is a String. */
    [[nodiscard]] bool IsString() const { return GetType() == ValueType::String; }
    /** @brief Checks if the value is a Table. */
    [[nodiscard]] bool IsTable() const { return GetType() == ValueType::Table; }
    /** @brief Checks if the value is a Function. */
    [[nodiscard]] bool IsFunction() const { return GetType() == ValueType::Function; }
    /** @brief Checks if the value is a Thread (Coroutine). */
    [[nodiscard]] bool IsThread() const { return GetType() == ValueType::Thread; }
    /** @brief Checks if the value is Userdata. */
    [[nodiscard]] bool IsUserdata() const { return GetType() == ValueType::Userdata; }
    /** @brief Checks if the value is a Buffer. */
    [[nodiscard]] bool IsBuffer() const { return GetType() == ValueType::Buffer; }

    /** @brief Casts the value to a boolean (unsafe, returns false if incorrect type). */
    [[nodiscard]] bool AsBoolean() const;
    /** @brief Casts the value to a double (unsafe, returns 0.0 if incorrect type). */
    [[nodiscard]] double AsNumber() const;
    /** @brief Casts the value to an integer (unsafe, returns 0 if incorrect type). */
    [[nodiscard]] int AsInteger() const;
    /** @brief Casts the value to a string (unsafe, returns "" if incorrect type). */
    [[nodiscard]] std::string AsString() const;
    /** @brief Casts the value to a LightUserdata pointer. */
    [[nodiscard]] void* AsLightUserdata() const;
    /** @brief Casts the value to a Buffer pointer and outputs its size. */
    [[nodiscard]] void* AsBuffer(size_t* sizeOut = nullptr) const;
    /** @brief Returns a zero-copy span if the value is a Buffer, otherwise empty. */
    [[nodiscard]] std::span<uint8_t> AsSpan() const;
    
    /**
     * @brief Converts to a Table.
     * @return The Table, or an empty Table if the value is not of table type.
     */
    [[nodiscard]] Table AsTable() const;

    /**
     * @brief Converts to a Buffer object.
     * @return The Buffer, or an empty Buffer if the value is not of buffer type.
     */
    [[nodiscard]] Buffer AsBufferObj() const;

    /** @brief Safely attempts to cast to boolean. */
    [[nodiscard]] Result<bool> TryAsBoolean() const;
    /** @brief Safely attempts to cast to double. */
    [[nodiscard]] Result<double> TryAsNumber() const;
    /** @brief Safely attempts to cast to integer. */
    [[nodiscard]] Result<int> TryAsInteger() const;
    /** @brief Safely attempts to cast to string. */
    [[nodiscard]] Result<std::string> TryAsString() const;
    /** @brief Safely attempts to cast to Buffer pointer. */
    [[nodiscard]] Result<void*> TryAsBuffer(size_t* sizeOut = nullptr) const;
    /** @brief Safely attempts to convert to a Buffer object. */
    [[nodiscard]] Result<Buffer> TryAsBufferObj() const;

    /**
     * @brief Calls the value if it's a function, explicitly providing a State.
     * @param vm The state to execute in.
     * @param args The arguments to pass.
     * @return Result containing a vector of returned Values.
     */
    Result<std::vector<Value>> Call(State& vm, const std::vector<Value>& args = {}) const;
    
    /**
     * @brief Calls the value if it's a function using its captured State.
     * @param args The arguments to pass.
     * @return Result containing a vector of returned Values.
     */
    Result<std::vector<Value>> Call(const std::vector<Value>& args = {}) const;

    /** @brief Fast zero-allocation call overload (Returns single Value). */
    Result<Value> CallSingle() const;
    /** @brief Fast zero-allocation call overload (Returns single Value) with 1 argument. */
    Result<Value> CallSingle(const Value& arg1) const;
    /** @brief Fast zero-allocation call overload (Returns single Value) with 2 arguments. */
    Result<Value> CallSingle(const Value& arg1, const Value& arg2) const;
    /** @brief Fast zero-allocation call overload (Returns single Value) with 3 arguments. */
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
