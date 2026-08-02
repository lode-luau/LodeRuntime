#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include <string>
#include <functional>
#include <vector>

struct lua_State;

namespace Lode
{

class LODE_API Exports
{
public:
    explicit Exports(State& vm);
    explicit Exports(lua_State* L);

    // High-level lambda/function wrapper accepting Lode::State & vector of Lode::Value
    void Function(const std::string& name, const std::function<Value(State& vm, const std::vector<Value>& args)>& fn);
    
    // High-level lambda taking no args returning Lode::Value
    void Function(const std::string& name, const std::function<Value()>& fn);

    // High-level lambda taking std::string returning std::string
    void Function(const std::string& name, const std::function<std::string(const std::string&)>& fn);

    // High-level lambda taking double returning double
    void Function(const std::string& name, const std::function<double(double)>& fn);

    void SetTable(const std::string& name, const Lode::Table& table);
    void SetValue(const std::string& name, const Lode::Value& value);

    [[nodiscard]] Lode::Table GetExportTable() const { return exportsTable_; }

private:
    lua_State* L_;
    Lode::Table exportsTable_;
};

} // namespace Lode

void LodeModuleRegister(Lode::State& vm, Lode::Exports& exports);

#define LODE_MODULE(vm_var, exports_var) \
    LODE_EXPORT int LodeModuleInit(lua_State* L) { \
        Lode::State vm_var(L); \
        Lode::Exports exports_var(vm_var); \
        LodeModuleRegister(vm_var, exports_var); \
        exports_var.GetExportTable().PushToLuaState(L); \
        return 1; \
    } \
    void LodeModuleRegister(Lode::State& vm_var, Lode::Exports& exports_var)
