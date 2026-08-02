// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "Lode/ClassBuilder.hpp"
#include "lua.h"
#include <string>
#include <functional>
#include <vector>
#include <initializer_list>

namespace Lode
{

class LODE_API ModuleReturn
{
public:
    ModuleReturn() = default;
    ModuleReturn(const Value& val) : values_({ val }) {}
    ModuleReturn(const Table& tbl) : values_({ Value(tbl) }) {}
    ModuleReturn(int num) : values_({ Value(num) }) {}
    ModuleReturn(double num) : values_({ Value(num) }) {}
    ModuleReturn(const std::string& str) : values_({ Value(str) }) {}
    ModuleReturn(bool b) : values_({ Value(b) }) {}
    ModuleReturn(std::initializer_list<Value> list) : values_(list) {}
    ModuleReturn(std::vector<Value> vals) : values_(std::move(vals)) {}

    [[nodiscard]] const std::vector<Value>& GetValues() const { return values_; }

private:
    std::vector<Value> values_;
};

class LODE_API Exports
{
public:
    explicit Exports(State& vm);
    explicit Exports(lua_State* L);

    void Function(const std::string& name, const std::function<Value(State& vm, const std::vector<Value>& args)>& fn);
    void Function(const std::string& name, const std::function<Value()>& fn);
    void Function(const std::string& name, const std::function<std::string(const std::string&)>& fn);
    void Function(const std::string& name, const std::function<double(double)>& fn);

    void SetTable(const std::string& name, const Lode::Table& table);
    void SetValue(const std::string& name, const Lode::Value& value);

    [[nodiscard]] Lode::Table GetExportTable() const { return exportsTable_; }

private:
    lua_State* L_;
    Lode::Table exportsTable_;
};

} // namespace Lode

Lode::ModuleReturn LodeModuleRegister(Lode::State& vm);

// --- Single LODE_MODULE Unified Macro with Automatic Compile-Time Detection ---
#if defined(LODE_STATIC_BUILD) || defined(LODE_STATIC)

#define LODE_MODULE_NAMED(name, vm_var) \
    static Lode::ModuleReturn LodeModuleRegister_##name(Lode::State& vm_var); \
    static int LodeModuleInit_##name(lua_State* L) { \
        Lode::State vm_var(L); \
        Lode::ModuleReturn ret = LodeModuleRegister_##name(vm_var); \
        for (const auto& val : ret.GetValues()) { \
            val.PushToLuaState(L); \
        } \
        return static_cast<int>(ret.GetValues().size()); \
    } \
    struct LodeStaticAutoRegister_##name { \
        LodeStaticAutoRegister_##name() { \
            ::Lode::NativeModuleRegistry::GetGlobalRegistry().RegisterStaticModule(#name, LodeModuleInit_##name); \
            ::Lode::NativeModuleRegistry::GetGlobalRegistry().RegisterStaticModule("./" #name, LodeModuleInit_##name); \
        } \
    } g_lodeStaticAutoRegister_##name; \
    static Lode::ModuleReturn LodeModuleRegister_##name(Lode::State& vm_var)

#define LODE_MODULE(vm_var) LODE_MODULE_NAMED(default_module, vm_var)

#else

#define LODE_MODULE_NAMED(name, vm_var) \
    LODE_EXPORT int LodeModuleInit(lua_State* L) { \
        Lode::State vm_var(L); \
        Lode::ModuleReturn ret = LodeModuleRegister(vm_var); \
        for (const auto& val : ret.GetValues()) { \
            val.PushToLuaState(L); \
        } \
        return static_cast<int>(ret.GetValues().size()); \
    } \
    Lode::ModuleReturn LodeModuleRegister(Lode::State& vm_var)

#define LODE_MODULE(vm_var) \
    LODE_EXPORT int LodeModuleInit(lua_State* L) { \
        Lode::State vm_var(L); \
        Lode::ModuleReturn ret = LodeModuleRegister(vm_var); \
        for (const auto& val : ret.GetValues()) { \
            val.PushToLuaState(L); \
        } \
        return static_cast<int>(ret.GetValues().size()); \
    } \
    Lode::ModuleReturn LodeModuleRegister(Lode::State& vm_var)

#endif
