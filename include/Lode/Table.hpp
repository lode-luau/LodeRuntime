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
#include <utility>

struct lua_State;

namespace Lode
{

class Metatable;
namespace Detail { struct PinnedRef; }

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
     * @param funcName The function field to call.
     * @param args Arguments to pass; each is converted to a Value. A std::vector<Value>
     *             passed as a single argument is spread as multiple arguments.
     * @return Result containing all returned Values.
     */
    template <typename... Args>
    Result<std::vector<Value>> CallFunction(State& vm, const std::string& funcName, Args&&... args) const
    {
        auto fnRes = Get(funcName);
        if (fnRes.IsError()) return fnRes.GetError();
        return fnRes.GetValue().Call(vm, std::forward<Args>(args)...);
    }

    /** @brief Calls a function stored in this table using its captured State. */
    template <typename... Args>
    Result<std::vector<Value>> CallFunction(const std::string& funcName, Args&&... args) const
    {
        auto fnRes = Get(funcName);
        if (fnRes.IsError()) return fnRes.GetError();
        return fnRes.GetValue().Call(std::forward<Args>(args)...);
    }

    /** 
     * @brief Calls a method stored in this table (like table:methodName(args)), injecting 'self' automatically. 
     * @param vm The state to execute in.
     * @param methodName The method field to call.
     * @param args Arguments to pass after the injected self; each is converted to a Value.
     * @return Result containing all returned Values.
     */
    template <typename... Args>
    Result<std::vector<Value>> CallMethod(State& vm, const std::string& methodName, Args&&... args) const
    {
        return CallFunction(vm, methodName, Value(*this), std::forward<Args>(args)...);
    }

    /** @brief Calls a method stored in this table using its captured State, injecting 'self' automatically. */
    template <typename... Args>
    Result<std::vector<Value>> CallMethod(const std::string& methodName, Args&&... args) const
    {
        return CallFunction(methodName, Value(*this), std::forward<Args>(args)...);
    }

    /**
     * @brief Calls a function stored in this table and returns only the first result.
     * @param vm The state to execute in.
     * @param funcName The function field to call.
     * @param args Arguments to pass; each is converted to a Value.
     * @return Result containing the first returned Value (Nil if the function returns none).
     */
    template <typename... Args>
    Result<Value> CallFunctionSingle(State& vm, const std::string& funcName, Args&&... args) const
    {
        auto fnRes = Get(funcName);
        if (fnRes.IsError()) return fnRes.GetError();
        return fnRes.GetValue().CallSingle(vm, std::forward<Args>(args)...);
    }

    /** @brief Calls a function stored in this table using its captured State, returning only the first result. */
    template <typename... Args>
    Result<Value> CallFunctionSingle(const std::string& funcName, Args&&... args) const
    {
        auto fnRes = Get(funcName);
        if (fnRes.IsError()) return fnRes.GetError();
        return fnRes.GetValue().CallSingle(std::forward<Args>(args)...);
    }

    /**
     * @brief Calls a method stored in this table (injecting 'self') and returns only the first result.
     * @param vm The state to execute in.
     * @param methodName The method field to call.
     * @param args Arguments to pass after the injected self; each is converted to a Value.
     * @return Result containing the first returned Value (Nil if the method returns none).
     */
    template <typename... Args>
    Result<Value> CallMethodSingle(State& vm, const std::string& methodName, Args&&... args) const
    {
        return CallFunctionSingle(vm, methodName, Value(*this), std::forward<Args>(args)...);
    }

    /** @brief Calls a method stored in this table using its captured State (injecting 'self'), returning only the first result. */
    template <typename... Args>
    Result<Value> CallMethodSingle(const std::string& methodName, Args&&... args) const
    {
        return CallFunctionSingle(methodName, Value(*this), std::forward<Args>(args)...);
    }

    void PushToLuaState(lua_State* L) const;
    [[nodiscard]] lua_State* GetLuaState() const;

private:
    std::shared_ptr<Detail::PinnedRef> refData_;
};

} // namespace Lode
