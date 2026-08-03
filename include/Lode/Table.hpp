// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include <string>
#include <memory>
#include <vector>

struct lua_State;

namespace Lode
{

class Metatable;

/**
 * @brief Represents a Luau table mapped to C++.
 * 
 * Provides methods for safely getting and setting fields, calling methods, 
 * and interacting with metatables. Memory is pinned in Lua GC via std::shared_ptr.
 */
class LODE_API Table
{
public:
    /** @brief Constructs an empty, invalid Table. */
    Table();
    /** @brief Constructs a Table by capturing it from the lua_State at the given index. */
    Table(lua_State* L, int index);
    ~Table();

    Table(const Table& other);
    Table(Table&& other) noexcept;
    Table& operator=(const Table& other);
    Table& operator=(Table&& other) noexcept;

    /** @brief Sets a string key to a Value. */
    void Set(const std::string& key, const Value& value);
    /** @brief Sets an integer key (array index) to a Value. */
    void Set(int key, const Value& value);

    /** @brief Retrieves the Value associated with a string key. */
    [[nodiscard]] Result<Value> Get(const std::string& key) const;
    /** @brief Retrieves the Value associated with an integer key (array index). */
    [[nodiscard]] Result<Value> Get(int key) const;

    /** @brief Checks if a string key exists and is not nil. */
    [[nodiscard]] bool Has(const std::string& key) const;
    /** @brief Returns the length of the table (like the '#' operator in Lua). */
    [[nodiscard]] size_t Size() const;
    /** @brief Retrieves all string keys present in the table. */
    [[nodiscard]] std::vector<std::string> GetKeys() const;

    /** @brief Sets the metatable using another Table. */
    void SetMetatable(const Table& metatable);
    /** @brief Sets the metatable using a Metatable object. */
    void SetMetatable(const Metatable& metatable);
    /** @brief Retrieves the current metatable. */
    [[nodiscard]] Result<Table> GetMetatable() const;

    /** 
     * @brief Calls a function stored in this table (like table.funcName(args)) 
     * @param vm The state to execute in.
     */
    Result<std::vector<Value>> CallFunction(State& vm, const std::string& funcName, const std::vector<Value>& args = {}) const;
    /** @brief Calls a function stored in this table using its captured State. */
    Result<std::vector<Value>> CallFunction(const std::string& funcName, const std::vector<Value>& args = {}) const;

    /** 
     * @brief Calls a method stored in this table (like table:methodName(args)), injecting 'self' automatically. 
     * @param vm The state to execute in.
     */
    Result<std::vector<Value>> CallMethod(State& vm, const std::string& methodName, const std::vector<Value>& args = {}) const;
    /** @brief Calls a method stored in this table using its captured State. */
    Result<std::vector<Value>> CallMethod(const std::string& methodName, const std::vector<Value>& args = {}) const;

    /** @brief Fast zero-allocation static function call overload (Returns single Value). */
    Result<Value> CallFunctionSingle(const std::string& funcName) const;
    /** @brief Fast zero-allocation static function call overload (Returns single Value) with 1 argument. */
    Result<Value> CallFunctionSingle(const std::string& funcName, const Value& arg1) const;
    /** @brief Fast zero-allocation static function call overload (Returns single Value) with 2 arguments. */
    Result<Value> CallFunctionSingle(const std::string& funcName, const Value& arg1, const Value& arg2) const;
    /** @brief Fast zero-allocation static function call overload (Returns single Value) with 3 arguments. */
    Result<Value> CallFunctionSingle(const std::string& funcName, const Value& arg1, const Value& arg2, const Value& arg3) const;

    /** @brief Fast zero-allocation method call overload (Returns single Value). */
    Result<Value> CallMethodSingle(const std::string& methodName) const;
    /** @brief Fast zero-allocation method call overload (Returns single Value) with 1 argument. */
    Result<Value> CallMethodSingle(const std::string& methodName, const Value& arg1) const;
    /** @brief Fast zero-allocation method call overload (Returns single Value) with 2 arguments. */
    Result<Value> CallMethodSingle(const std::string& methodName, const Value& arg1, const Value& arg2) const;
    /** @brief Fast zero-allocation method call overload (Returns single Value) with 3 arguments. */
    Result<Value> CallMethodSingle(const std::string& methodName, const Value& arg1, const Value& arg2, const Value& arg3) const;

    void PushToLuaState(lua_State* L) const;
    [[nodiscard]] lua_State* GetLuaState() const;

private:
    struct RefData;
    std::shared_ptr<RefData> refData_;
};

} // namespace Lode
