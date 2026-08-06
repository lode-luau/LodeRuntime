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
#include <array>
#include <cstdint>
#include <utility>
#include <type_traits>

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
    Vector,
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

/** @brief Stores a Luau vector with its configured component count. */
struct Vector
{
    std::array<float, 4> components{};
    size_t size = 3;
};

namespace Detail
{
    class SmallValueList;
    struct StateLifetime;
}

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
    /** @brief Constructs an Integer value from a 64-bit integer. */
    Value(int64_t i);
    /** @brief Constructs a Luau vector value. */
    Value(const Vector& vector);
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
    /** @brief Checks if the value is a Luau vector. */
    [[nodiscard]] bool IsVector() const { return GetType() == ValueType::Vector; }
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
    [[nodiscard]] int64_t AsInteger() const;
    /** @brief Casts the value to a Luau vector (unsafe, returns a zero vector if incorrect type). */
    [[nodiscard]] Vector AsVector() const;
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
    [[nodiscard]] Coroutine AsCoroutine() const;

    /** @brief Safely attempts to cast to boolean. */
    [[nodiscard]] Result<bool> TryAsBoolean() const;
    /** @brief Safely attempts to cast to double. */
    [[nodiscard]] Result<double> TryAsNumber() const;
    /** @brief Safely attempts to cast to integer. */
    [[nodiscard]] Result<int64_t> TryAsInteger() const;
    /** @brief Safely attempts to cast to a Luau vector. */
    [[nodiscard]] Result<Vector> TryAsVector() const;
    /** @brief Safely attempts to cast to string. */
    [[nodiscard]] Result<std::string> TryAsString() const;
    /** @brief Safely attempts to cast to Buffer pointer. */
    [[nodiscard]] Result<void*> TryAsBuffer(size_t* sizeOut = nullptr) const;
    /** @brief Safely attempts to convert to a Buffer object. */
    [[nodiscard]] Result<Buffer> TryAsBufferObj() const;

    /**
     * @brief Calls the value if it's a function, explicitly providing a State.
     * @param vm The state to execute in.
     * @param args Arguments to pass; each is converted to a Value. A std::vector<Value>
     *             passed as a single argument is spread as multiple arguments.
     * @return Result containing all returned Values.
     */
    template <typename... Args>
    Result<std::vector<Value>> Call(State& vm, Args&&... args) const;

    /**
     * @brief Calls the value if it's a function using its captured State.
     * @param args Arguments to pass; each is converted to a Value. A std::vector<Value>
     *             passed as a single argument is spread as multiple arguments.
     * @return Result containing all returned Values.
     */
    template <typename... Args>
    Result<std::vector<Value>> Call(Args&&... args) const;

    /**
     * @brief Calls the value and returns only the first result.
     * @param vm The state to execute in.
     * @param args Arguments to pass; each is converted to a Value.
     * @return Result containing the first returned Value (Nil if the function returns none).
     */
    template <typename... Args>
    Result<Value> CallSingle(State& vm, Args&&... args) const;

    /**
     * @brief Calls the value and returns only the first result, using its captured State.
     * @param args Arguments to pass; each is converted to a Value.
     * @return Result containing the first returned Value (Nil if the function returns none).
     */
    template <typename... Args>
    Result<Value> CallSingle(Args&&... args) const;


    // Internal creation for Luau stack values
    static Value FromLuaState(lua_State* L, int index);
    void PushToLuaState(lua_State* L) const;

private:
    // Shared implementation used by the Call/CallSingle templates.
    Result<std::vector<Value>> CallArgs(State& vm, Detail::SmallValueList args) const;
    Result<std::vector<Value>> CallArgs(Detail::SmallValueList args) const;
    Result<Value> CallSingleArgs(State& vm, Detail::SmallValueList args) const;
    Result<Value> CallSingleArgs(Detail::SmallValueList args) const;

    // Returns the lua_State* pinned by this value's reference, or nullptr.
    lua_State* GetCapturedState() const;

    struct RefData
    {
        lua_State* L = nullptr;
        lua_State* thread = nullptr;
        int refId = -1;
        std::shared_ptr<Detail::StateLifetime> lifetime;
        ~RefData();
    };

    ValueType type_ = ValueType::Nil;
    std::variant<std::monostate, bool, double, int64_t, Vector, std::string, void*, std::shared_ptr<RefData>> data_;
};

namespace Detail
{
    // Most native calls use only a handful of arguments. Keep those values in
    // the caller's stack frame and spill to a vector only for larger calls.
    class SmallValueList
    {
    public:
        static constexpr size_t InlineCapacity = 8;

        SmallValueList() = default;
        SmallValueList(const SmallValueList&) = default;
        SmallValueList(SmallValueList&&) noexcept = default;
        SmallValueList& operator=(const SmallValueList&) = default;
        SmallValueList& operator=(SmallValueList&&) noexcept = default;

        void push_back(Value value)
        {
            if (overflow_.empty() && size_ < InlineCapacity)
            {
                inlineValues_[size_++] = std::move(value);
                return;
            }

            if (overflow_.empty())
            {
                overflow_.reserve(InlineCapacity * 2);
                for (size_t i = 0; i < size_; ++i)
                    overflow_.push_back(std::move(inlineValues_[i]));
            }
            overflow_.push_back(std::move(value));
            ++size_;
        }

        [[nodiscard]] size_t size() const { return size_; }

        [[nodiscard]] const Value& operator[](size_t index) const
        {
            return overflow_.empty() ? inlineValues_[index] : overflow_[index];
        }

        [[nodiscard]] std::span<const Value> AsSpan() const
        {
            if (overflow_.empty())
                return std::span<const Value>(inlineValues_.data(), size_);
            return std::span<const Value>(overflow_.data(), overflow_.size());
        }

    private:
        std::array<Value, InlineCapacity> inlineValues_{};
        std::vector<Value> overflow_;
        size_t size_ = 0;
    };

    // Appends arguments to the call list. A std::vector<Value> passed as a single
    // argument is spread as multiple arguments, so callers that already hold the
    // argument list as a vector keep working without wrapping it.
    inline void AppendArgs(SmallValueList&) {}

    template <typename T, typename... Rest>
    void AppendArgs(SmallValueList& out, T&& arg, Rest&&... rest)
    {
        if constexpr (std::is_same_v<std::decay_t<T>, std::vector<Value>>)
        {
            for (const Value& value : arg)
                out.push_back(value);
        }
        else
        {
            out.push_back(Value(std::forward<T>(arg)));
        }
        AppendArgs(out, std::forward<Rest>(rest)...);
    }

    // Collects call arguments without allocating for the common small-call case.
    template <typename... Args>
    SmallValueList MakeArgs(Args&&... args)
    {
        SmallValueList out;
        AppendArgs(out, std::forward<Args>(args)...);
        return out;
    }
} // namespace Detail

template <typename... Args>
Result<std::vector<Value>> Value::Call(State& vm, Args&&... args) const
{
    return CallArgs(vm, Detail::MakeArgs(std::forward<Args>(args)...));
}

template <typename... Args>
Result<std::vector<Value>> Value::Call(Args&&... args) const
{
    return CallArgs(Detail::MakeArgs(std::forward<Args>(args)...));
}

template <typename... Args>
Result<Value> Value::CallSingle(State& vm, Args&&... args) const
{
    return CallSingleArgs(vm, Detail::MakeArgs(std::forward<Args>(args)...));
}

template <typename... Args>
Result<Value> Value::CallSingle(Args&&... args) const
{
    return CallSingleArgs(Detail::MakeArgs(std::forward<Args>(args)...));
}

} // namespace Lode
