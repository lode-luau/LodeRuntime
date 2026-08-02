#include "Lode/Table.hpp"
#include "lua.h"
#include "lualib.h"
#include <stdexcept>

namespace Lode
{

struct Table::RefData
{
    lua_State* L = nullptr;
    int refId = -1;

    ~RefData()
    {
        if (L && refId != LUA_NOREF && refId != LUA_REFNIL)
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
        refData_->L = L;
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

void Table::PushToLuaState(lua_State* L) const
{
    if (refData_ && refData_->refId != LUA_NOREF)
    {
        lua_getref(L, refData_->refId);
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
