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

class LODE_API Table
{
public:
    Table();
    Table(lua_State* L, int index);
    ~Table();

    Table(const Table& other);
    Table(Table&& other) noexcept;
    Table& operator=(const Table& other);
    Table& operator=(Table&& other) noexcept;

    void Set(const std::string& key, const Value& value);
    void Set(int key, const Value& value);

    [[nodiscard]] Result<Value> Get(const std::string& key) const;
    [[nodiscard]] Result<Value> Get(int key) const;

    [[nodiscard]] bool Has(const std::string& key) const;
    [[nodiscard]] size_t Size() const;
    [[nodiscard]] std::vector<std::string> GetKeys() const;

    void SetMetatable(const Table& metatable);
    void SetMetatable(const Metatable& metatable);
    [[nodiscard]] Result<Table> GetMetatable() const;

    Result<std::vector<Value>> CallMethod(State& vm, const std::string& methodName, const std::vector<Value>& args = {}) const;

    void PushToLuaState(lua_State* L) const;
    [[nodiscard]] lua_State* GetLuaState() const;

private:
    struct RefData;
    std::shared_ptr<RefData> refData_;
};

} // namespace Lode
