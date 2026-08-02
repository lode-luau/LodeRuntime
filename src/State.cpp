#include "Lode/State.hpp"
#include "Registry.hpp"
#include "ModuleLoader.hpp"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include <vector>
#include <string>
#include <iostream>

namespace Lode
{

struct State::Impl
{
    NativeModuleRegistry registry;
    std::vector<std::string> modulePaths;
};

State::State() : ownsState_(true), impl_(std::make_unique<Impl>())
{
    L_ = luaL_newstate();
    if (L_)
    {
        luaL_openlibs(L_);
        SetupModuleLoader(L_, &impl_->registry, impl_->modulePaths);
    }
}

State::State(lua_State* L) : L_(L), ownsState_(false), impl_(nullptr)
{
}

State::~State()
{
    if (L_ && ownsState_ && impl_)
    {
        impl_->registry.Clear();
        lua_close(L_);
        L_ = nullptr;
    }
}

State::State(State&& other) noexcept
    : L_(other.L_)
    , ownsState_(other.ownsState_)
    , impl_(std::move(other.impl_))
{
    other.L_ = nullptr;
    other.ownsState_ = false;
}

State& State::operator=(State&& other) noexcept
{
    if (this != &other)
    {
        if (L_ && ownsState_ && impl_)
        {
            impl_->registry.Clear();
            lua_close(L_);
        }
        L_ = other.L_;
        ownsState_ = other.ownsState_;
        impl_ = std::move(other.impl_);
        other.L_ = nullptr;
        other.ownsState_ = false;
    }
    return *this;
}

Result<State> State::Create()
{
    State state;
    if (!state.GetLuaState())
    {
        return Error::Runtime("Failed to initialize Luau lua_State");
    }
    return std::move(state);
}

Result<void> State::ExecuteBytecode(std::string_view bytecode, std::string_view chunkName)
{
    auto res = ExecuteBytecodeWithResults(bytecode, chunkName);
    if (res.IsError()) return res.GetError();
    return Result<void>();
}

Result<int> State::ExecuteBytecodeWithResults(std::string_view bytecode, std::string_view chunkName)
{
    if (!L_) return Error::Runtime("State VM is invalid");

    std::string name(chunkName);
    int loadRes = luau_load(L_, name.c_str(), bytecode.data(), bytecode.size(), 0);
    if (loadRes != 0)
    {
        std::string errStr = lua_tostring(L_, -1);
        Pop(1);
        return Error::Runtime("Failed to load Luau bytecode: " + errStr);
    }

    int topBefore = GetTop() - 1;
    int pcallRes = lua_pcall(L_, 0, LUA_MULTRET, 0);
    if (pcallRes != 0)
    {
        std::string errStr = lua_tostring(L_, -1);
        Pop(1);
        return Error::Runtime("Execution error: " + errStr);
    }

    int nresults = GetTop() - topBefore;
    if (nresults == 0)
    {
        PushNil();
        nresults = 1;
    }
    return nresults;
}

Result<Value> State::ProtectedCall(std::string_view bytecode, std::string_view chunkName)
{
    auto res = ExecuteBytecodeWithResults(bytecode, chunkName);
    if (res.IsError()) return res.GetError();
    int count = res.GetValue();
    if (count <= 0) return Value();

    Value val = Value::FromLuaState(L_, -1);
    Pop(count);
    return val;
}

Result<std::vector<Value>> State::CallFunction(const Value& fn, const std::vector<Value>& args)
{
    if (!L_) return Error::Runtime("State VM is invalid");

    int topBefore = GetTop();
    fn.PushToLuaState(L_);
    for (const auto& arg : args)
    {
        arg.PushToLuaState(L_);
    }

    int pcallRes = lua_pcall(L_, static_cast<int>(args.size()), LUA_MULTRET, 0);
    if (pcallRes != 0)
    {
        std::string errStr = lua_tostring(L_, -1);
        Pop(1);
        return Error::Runtime("Function call error: " + errStr);
    }

    int nresults = GetTop() - topBefore;
    std::vector<Value> results;
    results.reserve(nresults);
    for (int i = topBefore + 1; i <= GetTop(); ++i)
    {
        results.push_back(Value::FromLuaState(L_, i));
    }
    Pop(nresults);
    return results;
}

void State::RaiseError(std::string_view message)
{
    if (L_)
    {
        std::string msg(message);
        luaL_error(L_, "%s", msg.c_str());
    }
}

// --- Stack Manipulation API ---
int State::GetTop() const { return L_ ? lua_gettop(L_) : 0; }
void State::SetTop(int index) { if (L_) lua_settop(L_, index); }
void State::Pop(int count) { if (L_ && count > 0) lua_pop(L_, count); }
void State::Remove(int index) { if (L_) lua_remove(L_, index); }
void State::Insert(int index) { if (L_) lua_insert(L_, index); }
void State::Replace(int index) { if (L_) lua_replace(L_, index); }

// --- Stack Push API ---
void State::PushNil() { if (L_) lua_pushnil(L_); }
void State::PushBoolean(bool b) { if (L_) lua_pushboolean(L_, b ? 1 : 0); }
void State::PushNumber(double n) { if (L_) lua_pushnumber(L_, n); }
void State::PushInteger(int i) { if (L_) lua_pushinteger(L_, i); }
void State::PushString(std::string_view str) { if (L_) lua_pushlstring(L_, str.data(), str.size()); }
void State::PushLightUserdata(void* ptr) { if (L_) lua_pushlightuserdata(L_, ptr); }
void State::PushValue(const Value& val) { if (L_) val.PushToLuaState(L_); }
void State::PushValues(const std::vector<Value>& values) { for (const auto& val : values) PushValue(val); }
void State::PushTable(const Table& table) { if (L_) table.PushToLuaState(L_); }

// --- Stack Type Inspection API ---
bool State::IsNil(int index) const { return L_ ? lua_isnil(L_, index) : false; }
bool State::IsBoolean(int index) const { return L_ ? lua_isboolean(L_, index) : false; }
bool State::IsNumber(int index) const { return L_ ? lua_isnumber(L_, index) : false; }
bool State::IsInteger(int index) const { return L_ ? lua_isnumber(L_, index) : false; }
bool State::IsString(int index) const { return L_ ? lua_isstring(L_, index) : false; }
bool State::IsTable(int index) const { return L_ ? lua_istable(L_, index) : false; }
bool State::IsFunction(int index) const { return L_ ? lua_isfunction(L_, index) : false; }
bool State::IsThread(int index) const { return L_ ? lua_isthread(L_, index) : false; }
bool State::IsUserdata(int index) const { return L_ ? lua_isuserdata(L_, index) : false; }
bool State::IsLightUserdata(int index) const { return L_ ? lua_islightuserdata(L_, index) : false; }

// --- Stack Reading API ---
Value State::GetValue(int index) const { return L_ ? Value::FromLuaState(L_, index) : Value(); }
std::string State::GetString(int index) const { return L_ ? lua_tostring(L_, index) : ""; }
double State::GetNumber(int index) const { return L_ ? lua_tonumber(L_, index) : 0.0; }
int State::GetInteger(int index) const { return L_ ? static_cast<int>(lua_tointeger(L_, index)) : 0; }
bool State::GetBoolean(int index) const { return L_ ? static_cast<bool>(lua_toboolean(L_, index)) : false; }
void* State::GetLightUserdata(int index) const { return L_ ? lua_touserdata(L_, index) : nullptr; }

// --- Stack Table & Field API ---
void State::GetField(int index, const char* name) { if (L_) lua_getfield(L_, index, name); }
void State::SetField(int index, const char* name) { if (L_) lua_setfield(L_, index, name); }
void State::RawGet(int index, int n) { if (L_) lua_rawgeti(L_, index, n); }
void State::RawSet(int index, int n) { if (L_) lua_rawseti(L_, index, n); }

void State::AddModulePath(std::string_view path)
{
    if (impl_)
    {
        impl_->modulePaths.emplace_back(path);
    }
}

void State::SetGlobal(const std::string& name, const Value& value)
{
    if (!L_) return;
    value.PushToLuaState(L_);
    lua_setglobal(L_, name.c_str());
}

Result<Value> State::GetGlobal(const std::string& name) const
{
    if (!L_) return Error::Runtime("State VM is invalid");
    lua_getglobal(L_, name.c_str());
    Value val = Value::FromLuaState(L_, -1);
    const_cast<State*>(this)->Pop(1);
    return val;
}

Table State::CreateTable()
{
    if (!L_) return Table();
    lua_newtable(L_);
    Table t(L_, -1);
    Pop(1);
    return t;
}

Coroutine State::CreateCoroutine(const Value& fn)
{
    if (!L_) return Coroutine();
    fn.PushToLuaState(L_);
    int fnRef = lua_ref(L_, -1);
    Pop(1);
    return Coroutine(L_, fnRef);
}

Result<Value> State::Require(std::string_view moduleName)
{
    if (!L_) return Error::Runtime("State VM is invalid");
    lua_getglobal(L_, "require");
    if (!lua_isfunction(L_, -1))
    {
        Pop(1);
        return Error::Runtime("Require function is not available in Luau global scope");
    }

    std::string modStr(moduleName);
    lua_pushlstring(L_, modStr.data(), modStr.length());
    int res = lua_pcall(L_, 1, 1, 0);
    if (res != 0)
    {
        std::string errStr = lua_tostring(L_, -1);
        Pop(1);
        return Error::Runtime("Require failed: " + errStr);
    }

    Value val = Value::FromLuaState(L_, -1);
    Pop(1);
    return val;
}

} // namespace Lode
