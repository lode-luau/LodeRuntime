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

class LODE_API Metatable
{
public:
    explicit Metatable(State& vm);
    ~Metatable();

    Metatable(const Metatable& other);
    Metatable(Metatable&& other) noexcept;
    Metatable& operator=(const Metatable& other);
    Metatable& operator=(Metatable&& other) noexcept;

    void SetIndexTable(const Table& targetTable);
    void SetIndexFunction(const std::function<Value(State& vm, Value key)>& fn);
    void SetNewIndexFunction(const std::function<void(State& vm, Value key, Value val)>& fn);
    void SetToString(const std::function<std::string(State& vm)>& fn);
    void SetGC(const std::function<void(State& vm)>& fn);
    void SetCall(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn);
    void SetAdd(const std::function<Value(State& vm, Value a, Value b)>& fn);
    void SetSub(const std::function<Value(State& vm, Value a, Value b)>& fn);
    void SetMul(const std::function<Value(State& vm, Value a, Value b)>& fn);
    void SetDiv(const std::function<Value(State& vm, Value a, Value b)>& fn);
    void SetEq(const std::function<bool(State& vm, Value a, Value b)>& fn);

    void SetMetaMethod(const std::string& name, const Value& val);

    [[nodiscard]] Table GetTable() const { return table_; }

private:
    Table table_;
};

} // namespace Lode
