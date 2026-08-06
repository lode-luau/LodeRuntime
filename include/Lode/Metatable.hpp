// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <string>
#include <functional>
#include <vector>

namespace Lode
{

class State;

/**
 * @brief Helper class to easily construct and bind metamethods to a table.
 */
class LODE_API Metatable
{
public:
    /** @brief Constructs an empty Metatable wrapper linked to the given state. */
    explicit Metatable(State& vm);
    ~Metatable();

    Metatable(const Metatable& other);
    Metatable(Metatable&& other) noexcept;
    Metatable& operator=(const Metatable& other);
    Metatable& operator=(Metatable&& other) noexcept;

    /** @brief Sets __index to fall back to another table. */
    void SetIndexTable(const Table& targetTable);
    /** @brief Sets __index to call a C++ function when a key is missing. */
    void SetIndexFunction(const std::function<Value(State& vm, Value key)>& fn);
    /** @brief Sets __newindex to call a C++ function on new field creation. */
    void SetNewIndexFunction(const std::function<void(State& vm, Value key, Value val)>& fn);
    /** @brief Sets __tostring metamethod. */
    void SetToString(const std::function<std::string(State& vm)>& fn);
    /** @brief Deprecated: Luau does not run metatable __gc methods. */
    void SetGC(const std::function<void(State& vm)>& fn);
    /** @brief Sets __call metamethod to make the table callable like a function. */
    void SetCall(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn);
    /** @brief Sets __add metamethod. */
    void SetAdd(const std::function<Value(State& vm, Value a, Value b)>& fn);
    /** @brief Sets __sub metamethod. */
    void SetSub(const std::function<Value(State& vm, Value a, Value b)>& fn);
    /** @brief Sets __mul metamethod. */
    void SetMul(const std::function<Value(State& vm, Value a, Value b)>& fn);
    /** @brief Sets __div metamethod. */
    void SetDiv(const std::function<Value(State& vm, Value a, Value b)>& fn);
    /** @brief Sets __idiv metamethod. */
    void SetIntegerDivide(const std::function<Value(State& vm, Value a, Value b)>& fn);
    /** @brief Sets __len metamethod. */
    void SetLength(const std::function<Value(State& vm, Value object)>& fn);
    /** @brief Sets __iter metamethod. */
    void SetIter(const std::function<std::vector<Value>(State& vm, Value object)>& fn);
    /** @brief Sets __eq metamethod. */
    void SetEq(const std::function<bool(State& vm, Value a, Value b)>& fn);

    /** @brief Sets an arbitrary raw metamethod field (e.g. "__metatable"). */
    void SetMetaMethod(const std::string& name, const Value& val);

    /** @brief Retrieves the underlying built metatable. */
    [[nodiscard]] Table GetTable() const { return table_; }

private:
    Table table_;
};

} // namespace Lode
