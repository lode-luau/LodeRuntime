#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include <string>
#include <memory>

struct lua_State;

namespace Lode
{

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

    void PushToLuaState(lua_State* L) const;
    [[nodiscard]] lua_State* GetLuaState() const;

private:
    struct RefData;
    std::shared_ptr<RefData> refData_;
};

} // namespace Lode
