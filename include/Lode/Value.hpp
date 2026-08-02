#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include <string>
#include <memory>
#include <vector>

struct lua_State;

namespace Lode
{

enum class ValueType
{
    Nil,
    Boolean,
    Number,
    Integer,
    String,
    Table,
    Function,
    Thread,
    Userdata,
    LightUserdata
};

class State;
class Table;

class LODE_API Value
{
public:
    Value();
    Value(bool b);
    Value(double n);
    Value(int i);
    Value(const char* str);
    Value(const std::string& str);
    Value(void* lightUserdata);
    Value(const Table& table);

    ~Value();
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;

    [[nodiscard]] ValueType GetType() const;
    [[nodiscard]] bool IsNil() const { return GetType() == ValueType::Nil; }
    [[nodiscard]] bool IsBoolean() const { return GetType() == ValueType::Boolean; }
    [[nodiscard]] bool IsNumber() const { return GetType() == ValueType::Number || GetType() == ValueType::Integer; }
    [[nodiscard]] bool IsInteger() const { return GetType() == ValueType::Integer; }
    [[nodiscard]] bool IsString() const { return GetType() == ValueType::String; }
    [[nodiscard]] bool IsTable() const { return GetType() == ValueType::Table; }
    [[nodiscard]] bool IsFunction() const { return GetType() == ValueType::Function; }
    [[nodiscard]] bool IsThread() const { return GetType() == ValueType::Thread; }
    [[nodiscard]] bool IsUserdata() const { return GetType() == ValueType::Userdata; }

    [[nodiscard]] bool AsBoolean() const;
    [[nodiscard]] double AsNumber() const;
    [[nodiscard]] int AsInteger() const;
    [[nodiscard]] std::string AsString() const;
    [[nodiscard]] void* AsLightUserdata() const;

    [[nodiscard]] Result<bool> TryAsBoolean() const;
    [[nodiscard]] Result<double> TryAsNumber() const;
    [[nodiscard]] Result<int> TryAsInteger() const;
    [[nodiscard]] Result<std::string> TryAsString() const;

    // Invoke if value is a function
    Result<std::vector<Value>> Call(State& vm, const std::vector<Value>& args = {}) const;

    // Internal creation for Luau stack values
    static Value FromLuaState(lua_State* L, int index);
    void PushToLuaState(lua_State* L) const;

private:
    struct RefData
    {
        lua_State* L = nullptr;
        int refId = -1;
        ~RefData();
    };

    ValueType type_ = ValueType::Nil;
    bool boolVal_ = false;
    double numberVal_ = 0.0;
    int intVal_ = 0;
    std::string stringVal_;
    void* lightUserdataVal_ = nullptr;
    std::shared_ptr<RefData> refData_;
};

} // namespace Lode
