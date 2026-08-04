#pragma once

#include "Lode/Export.hpp"
#include "Lode/Value.hpp"
#include "Lode/Result.hpp"
#include <span>
#include <string_view>
#include <cstdint>

struct lua_State;

namespace Lode
{

/**
 * @brief Represents a lightweight, non-owning reference to a value on the Lua stack.
 * 
 * StackValue provides zero-allocation reads from the Lua stack. It is incredibly 
 * fast but should not be stored beyond the scope of the native callback.
 */
class LODE_API StackValue
{
public:
    /**
     * @brief Constructs a StackValue pointing to a specific stack index.
     * @param L The lua_State pointer.
     * @param index The index on the Lua stack.
     */
    StackValue(lua_State* L, int index);

    /** @brief Gets the type of the value on the stack. */
    [[nodiscard]] ValueType GetType() const;

    /** @brief Checks if the value is Nil. */
    [[nodiscard]] bool IsNil() const;
    /** @brief Checks if the value is a Boolean. */
    [[nodiscard]] bool IsBoolean() const;
    /** @brief Checks if the value is a Number. */
    [[nodiscard]] bool IsNumber() const;
    /** @brief Checks if the value is an Integer. */
    [[nodiscard]] bool IsInteger() const;
    /** @brief Checks if the value is a String. */
    [[nodiscard]] bool IsString() const;
    /** @brief Checks if the value is a Buffer. */
    [[nodiscard]] bool IsBuffer() const;
    /** @brief Checks if the value is a Table. */
    [[nodiscard]] bool IsTable() const;
    /** @brief Checks if the value is a Function. */
    [[nodiscard]] bool IsFunction() const;
    /** @brief Checks if the value is a Thread (Coroutine). */
    [[nodiscard]] bool IsThread() const;
    /** @brief Checks if the value is Userdata. */
    [[nodiscard]] bool IsUserdata() const;

    /** @brief Fast unsafe cast to boolean. */
    [[nodiscard]] bool AsBoolean() const;
    /** @brief Fast unsafe cast to double. */
    [[nodiscard]] double AsNumber() const;
    /** @brief Fast unsafe cast to integer. */
    [[nodiscard]] int AsInteger() const;
    /** @brief Fast unsafe cast to string. */
    [[nodiscard]] std::string AsString() const;
    /** @brief Zero-copy view into the string on the stack. */
    [[nodiscard]] std::string_view AsStringView() const;
    /** @brief Fast unsafe cast to Buffer pointer. */
    [[nodiscard]] void* AsBuffer(size_t* sizeOut = nullptr) const;
    /** @brief Zero-copy view into the buffer on the stack. */
    [[nodiscard]] std::span<uint8_t> AsSpan() const;

    /** @brief Safely attempts to read the value as a number. */
    [[nodiscard]] Result<double> TryAsNumber() const;

    /**
     * @brief Converts this stack reference into a fully-owned Value.
     * @note This may allocate a RefData object if the value is a table, function, or buffer.
     * @return A persistent Value object.
     */
    [[nodiscard]] Value ToValue() const;

private:
    lua_State* L_ = nullptr;
    int index_ = 0;
};

/**
     * @brief A lightweight, zero-allocation wrapper for arguments passed to a native function.
     */
class LODE_API StackArgs
{
public:
    /** @brief Captures the arguments currently on the Lua stack. */
    StackArgs(lua_State* L);

    /** @brief Returns the number of arguments provided. */
    [[nodiscard]] size_t Size() const;
    
    /** 
     * @brief Accesses an argument by index (0-based, like standard C++ arrays).
     * @param i The index of the argument.
     * @return A StackValue pointing to the argument.
     */
    [[nodiscard]] StackValue operator[](size_t i) const;

private:
    lua_State* L_ = nullptr;
    int numArgs_ = 0;
};

} // namespace Lode
