#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/Coroutine.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>

struct lua_State;

namespace Lode
{

class LODE_API State
{
public:
    State();
    explicit State(lua_State* L);
    ~State();

    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&& other) noexcept;
    State& operator=(State&& other) noexcept;

    static Result<State> Create();

    Result<void> ExecuteBytecode(std::string_view bytecode, std::string_view chunkName = "=main");
    Result<int> ExecuteBytecodeWithResults(std::string_view bytecode, std::string_view chunkName = "=main");
    Result<Value> ProtectedCall(std::string_view bytecode, std::string_view chunkName = "=main");
    Result<std::vector<Value>> CallFunction(const Value& fn, const std::vector<Value>& args = {});

    void AddModulePath(std::string_view path);

    void SetGlobal(const std::string& name, const Value& value);
    [[nodiscard]] Result<Value> GetGlobal(const std::string& name) const;

    [[nodiscard]] Table CreateTable();
    [[nodiscard]] Coroutine CreateCoroutine(const Value& fn);

    Result<Value> Require(std::string_view moduleName);

    void RaiseError(std::string_view message);

    // --- Stack Manipulation API ---
    [[nodiscard]] int GetTop() const;
    void SetTop(int index);
    void Pop(int count = 1);
    void Remove(int index);
    void Insert(int index);
    void Replace(int index);

    // --- Stack Push API ---
    void PushNil();
    void PushBoolean(bool b);
    void PushNumber(double n);
    void PushInteger(int i);
    void PushString(std::string_view str);
    void PushLightUserdata(void* ptr);
    void PushValue(const Value& val);
    void PushValues(const std::vector<Value>& values);
    void PushTable(const Table& table);

    // --- Stack Type Inspection API ---
    [[nodiscard]] bool IsNil(int index = -1) const;
    [[nodiscard]] bool IsBoolean(int index = -1) const;
    [[nodiscard]] bool IsNumber(int index = -1) const;
    [[nodiscard]] bool IsInteger(int index = -1) const;
    [[nodiscard]] bool IsString(int index = -1) const;
    [[nodiscard]] bool IsTable(int index = -1) const;
    [[nodiscard]] bool IsFunction(int index = -1) const;
    [[nodiscard]] bool IsThread(int index = -1) const;
    [[nodiscard]] bool IsUserdata(int index = -1) const;
    [[nodiscard]] bool IsLightUserdata(int index = -1) const;

    // --- Stack Reading API ---
    [[nodiscard]] Value GetValue(int index) const;
    [[nodiscard]] std::string GetString(int index) const;
    [[nodiscard]] double GetNumber(int index) const;
    [[nodiscard]] int GetInteger(int index) const;
    [[nodiscard]] bool GetBoolean(int index) const;
    [[nodiscard]] void* GetLightUserdata(int index) const;

    // --- Stack Table & Field API ---
    void GetField(int index, const char* name);
    void SetField(int index, const char* name);
    void RawGet(int index, int n);
    void RawSet(int index, int n);

    [[nodiscard]] lua_State* GetLuaState() const { return L_; }

private:
    struct Impl;
    lua_State* L_ = nullptr;
    bool ownsState_ = true;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lode
