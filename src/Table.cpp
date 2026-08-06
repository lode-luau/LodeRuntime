// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Table.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/State.hpp"
#include "lua.h"
#include "lualib.h"
#include "StateLifetime.hpp"
#include <stdexcept>

namespace Lode
{

struct Table::RefData
{
    lua_State* L = nullptr;
    int refId = -1;
    std::shared_ptr<Detail::StateLifetime> lifetime;

    ~RefData()
    {
        if (L && lifetime && lifetime->alive.load() && refId != LUA_NOREF && refId != LUA_REFNIL)
        {
            lua_unref(L, refId);
        }
    }
};

Table::Table() = default;

Table::Table(lua_State* L, int index)
{
    if (L && lua_istable(L, index))
    {
        refData_ = std::make_shared<RefData>();
        refData_->L = lua_mainthread(L);
        refData_->lifetime = Detail::GetStateLifetime(L);
        refData_->refId = lua_ref(L, index);
    }
}

Table::~Table() = default;
Table::Table(const Table& other) = default;
Table::Table(Table&& other) noexcept = default;
Table& Table::operator=(const Table& other) = default;
Table& Table::operator=(Table&& other) noexcept = default;

void Table::Set(const std::string& key, const Value& value)
{
    if (!refData_ || !refData_->L) return;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    value.PushToLuaState(L);
    lua_setfield(L, -2, key.c_str());
    lua_pop(L, 1);
}

void Table::Set(int key, const Value& value)
{
    if (!refData_ || !refData_->L) return;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    value.PushToLuaState(L);
    lua_rawseti(L, -2, key);
    lua_pop(L, 1);
}

Result<Value> Table::Get(const std::string& key) const
{
    if (!refData_ || !refData_->L) return Error::Runtime("Table is uninitialized");
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    lua_getfield(L, -1, key.c_str());
    Value val = Value::FromLuaState(L, -1);
    lua_pop(L, 2);
    return val;
}

Result<Value> Table::Get(int key) const
{
    if (!refData_ || !refData_->L) return Error::Runtime("Table is uninitialized");
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    lua_rawgeti(L, -1, key);
    Value val = Value::FromLuaState(L, -1);
    lua_pop(L, 2);
    return val;
}

bool Table::Has(const std::string& key) const
{
    if (!refData_ || !refData_->L) return false;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    lua_getfield(L, -1, key.c_str());
    bool exists = !lua_isnil(L, -1);
    lua_pop(L, 2);
    return exists;
}

size_t Table::Size() const
{
    if (!refData_ || !refData_->L) return 0;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    int len = lua_objlen(L, -1);
    lua_pop(L, 1);
    return static_cast<size_t>(len);
}

std::vector<std::string> Table::GetKeys() const
{
    std::vector<std::string> keys;
    if (!refData_ || !refData_->L) return keys;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            size_t length = 0;
            const char* key = lua_tolstring(L, -2, &length);
            if (key)
                keys.emplace_back(key, length);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return keys;
}

void Table::SetMetatable(const Table& metatable)
{
    if (!refData_ || !refData_->L) return;
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    metatable.PushToLuaState(L);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

void Table::SetMetatable(const Metatable& metatable)
{
    SetMetatable(metatable.GetTable());
}

Result<Table> Table::GetMetatable() const
{
    if (!refData_ || !refData_->L) return Error::Runtime("Table is uninitialized");
    lua_State* L = refData_->L;
    lua_getref(L, refData_->refId);
    if (lua_getmetatable(L, -1))
    {
        Table mt(L, -1);
        lua_pop(L, 2);
        return mt;
    }
    lua_pop(L, 1);
    return Error::Runtime("Table has no metatable");
}

void Table::PushToLuaState(lua_State* L) const
{
    if (!L)
        return;

    if (refData_ && refData_->L && refData_->refId != LUA_NOREF && refData_->refId != LUA_REFNIL)
    {
        if (lua_mainthread(L) != refData_->L)
        {
            lua_pushnil(L);
            return;
        }

        lua_getref(refData_->L, refData_->refId);
        if (L != refData_->L)
            lua_xmove(refData_->L, L, 1);
    }
    else
    {
        lua_pushnil(L);
    }
}

lua_State* Table::GetLuaState() const
{
    return refData_ ? refData_->L : nullptr;
}

} // namespace Lode
