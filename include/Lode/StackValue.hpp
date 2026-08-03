#pragma once

#include "Lode/Export.hpp"
#include "Lode/Value.hpp"
#include "Lode/Result.hpp"

struct lua_State;

namespace Lode
{

class LODE_API StackValue
{
public:
    StackValue(lua_State* L, int index);

    [[nodiscard]] ValueType GetType() const;

    [[nodiscard]] bool IsNil() const;
    [[nodiscard]] bool IsBoolean() const;
    [[nodiscard]] bool IsNumber() const;
    [[nodiscard]] bool IsInteger() const;
    [[nodiscard]] bool IsString() const;
    [[nodiscard]] bool IsBuffer() const;

    // Fast unsafe getters
    [[nodiscard]] bool AsBoolean() const;
    [[nodiscard]] double AsNumber() const;
    [[nodiscard]] int AsInteger() const;
    [[nodiscard]] std::string AsString() const;
    [[nodiscard]] void* AsBuffer(size_t* sizeOut = nullptr) const;

    // Fast safe getters
    [[nodiscard]] Result<double> TryAsNumber() const;

    // Convert to fully-owned Value (might allocate RefData for tables/functions)
    [[nodiscard]] Value ToValue() const;

private:
    lua_State* L_ = nullptr;
    int index_ = 0;
};

class LODE_API StackArgs
{
public:
    StackArgs(lua_State* L);

    [[nodiscard]] size_t Size() const;
    [[nodiscard]] StackValue operator[](size_t i) const;

private:
    lua_State* L_ = nullptr;
    int numArgs_ = 0;
};

} // namespace Lode
