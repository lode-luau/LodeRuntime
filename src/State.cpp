// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "ModuleLoader.hpp"
#include "Registry.hpp"
#include "LuaError.hpp"
#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "Luau/CodeGen.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Lode
{

struct State::Impl
{
    NativeModuleRegistry registry;
    std::vector<std::string> modulePaths;
    std::unique_ptr<EventLoop> ownedEventLoop;
    EventLoop* eventLoop = nullptr;
};

State::State() : L_(luaL_newstate()), ownsState_(true), impl_(std::make_unique<Impl>())
{
    if (L_)
    {
        impl_->ownedEventLoop = std::make_unique<EventLoop>();
        impl_->eventLoop = impl_->ownedEventLoop.get();
        lua_pushlightuserdata(L_, impl_->eventLoop);
        lua_setfield(L_, LUA_REGISTRYINDEX, "_LODE_EVENT_LOOP");
        luaL_openlibs(L_);
        SetupModuleLoader(L_, &impl_->registry, impl_->modulePaths);

        if (Luau::CodeGen::isSupported())
        {
            Luau::CodeGen::create(L_);
        }
    }
}

State::State(lua_State* L) : L_(L), ownsState_(false), impl_(std::make_unique<Impl>())
{
    if (L_)
    {
        lua_getfield(L_, LUA_REGISTRYINDEX, "_LODE_EVENT_LOOP");
        impl_->eventLoop = static_cast<EventLoop*>(lua_touserdata(L_, -1));
        lua_pop(L_, 1);
    }
}

lua_State* State::GetMainThread() const
{
    return L_ ? lua_mainthread(L_) : nullptr;
}

EventLoop& State::GetEventLoop() const
{
    if (!impl_ || !impl_->eventLoop)
        throw std::logic_error("State has no event loop");
    return *impl_->eventLoop;
}

State::~State()
{
    if (ownsState_ && L_)
    {
        // Cancel every pending timer for this State before the VM is closed.
        // Timers hold Value/Coroutine references to this lua_State, so they
        // must be released while the state is still open (see issue #9).
        Lode::Task::Shutdown(*this);
        if (impl_ && impl_->ownedEventLoop)
            impl_->ownedEventLoop->Close();
        lua_close(L_);
        L_ = nullptr;
    }
}

State::State(State&& other) noexcept
    : L_(other.L_), ownsState_(other.ownsState_), impl_(std::move(other.impl_))
{
    other.L_ = nullptr;
    other.ownsState_ = false;
}

State& State::operator=(State&& other) noexcept
{
    if (this != &other)
    {
        if (ownsState_ && L_)
        {
            Task::Shutdown(*this);
            if (impl_ && impl_->ownedEventLoop)
                impl_->ownedEventLoop->Close();
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
    State s;
    if (!s.L_)
    {
        return Error::Platform("Failed to initialize Luau VM state");
    }
    return s;
}

Result<void> State::ExecuteBytecode(std::string_view bytecode, std::string_view chunkName)
{
    auto res = ExecuteBytecodeWithResults(bytecode, chunkName, true);
    if (res.IsError()) return res.GetError();
    return Result<void>();
}

Result<int> State::ExecuteBytecodeWithResults(std::string_view bytecode, std::string_view chunkName, bool isMainScript)
{
    if (!L_) return Error::Runtime("State is null");

    // Wrap script execution in a Main Coroutine so root code can yield seamlessly
    lua_State* co = lua_newthread(L_);
    if (isMainScript)
    {
        Lode::Task::SetMainThread(*this, co);
    }
    int coRef = lua_ref(L_, -1);
    lua_pop(L_, 1);

    std::string nameStr(chunkName);
    int loadStatus = luau_load(co, nameStr.c_str(), bytecode.data(), bytecode.size(), 0);
    if (loadStatus != LUA_OK)
    {
        std::string errStr = LuaErrorMessage(co, -1);
        lua_pop(co, 1);
        lua_unref(L_, coRef);
        return Error::Syntax("Bytecode load failed: " + errStr);
    }

    if (Luau::CodeGen::isSupported())
    {
        Luau::CodeGen::compile(co, -1, Luau::CodeGen::CodeGen_OnlyNativeModules);
    }

    int resStatus = lua_resume(co, nullptr, 0);
    if (resStatus != LUA_OK && resStatus != LUA_YIELD)
    {
        std::string errStr = LuaErrorMessage(co, -1);
        lua_pop(co, 1);
        lua_unref(L_, coRef);
        return Error::Runtime("Execution failed: " + errStr);
    }

    int nresults = lua_gettop(co);
    if (nresults > 0)
    {
        lua_xmove(co, L_, nresults);
    }
    lua_unref(L_, coRef);
    return nresults;
}

Result<Value> State::ProtectedCall(std::string_view bytecode, std::string_view chunkName)
{
    auto res = ExecuteBytecodeWithResults(bytecode, chunkName);
    if (res.IsError()) return res.GetError();

    int nresults = res.GetValue();
    if (nresults > 0)
    {
        Value v = Value::FromLuaState(L_, -1);
        Pop(nresults);
        return v;
    }
    return Value();
}

void State::AddModulePath(std::string_view path)
{
    if (!impl_) return;
    impl_->modulePaths.push_back(std::string(path));
    // Keep the loader's navigation context in sync so require() can actually
    // fall back to the newly added search directories.
    UpdateModulePaths(L_, impl_->modulePaths);
}

void State::SetGlobal(const std::string& name, const Value& value)
{
    if (!L_) return;
    value.PushToLuaState(L_);
    lua_setglobal(L_, name.c_str());
}

Result<Value> State::GetGlobal(const std::string& name) const
{
    if (!L_) return Error::Runtime("State is null");
    lua_getglobal(L_, name.c_str());
    Value val = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return val;
}

Table State::CreateTable()
{
    if (!L_) return Table();
    lua_newtable(L_);
    Table t(L_, -1);
    lua_pop(L_, 1);
    return t;
}

Metatable State::CreateMetatable()
{
    return Metatable(*this);
}

Value State::CreateFunction(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn)
{
    if (!L_) return Value();

    struct ClosureData
    {
        std::function<Value(State& vm, const std::vector<Value>& args)> func;
    };
    // GC-tracked userdata owns the payload: it is freed (destructor runs) when
    // the closure is collected, instead of leaking on every CreateFunction call.
    auto* data = static_cast<ClosureData*>(lua_newuserdata(L_, sizeof(ClosureData)));
    new (data) ClosureData{ fn };

    lua_newtable(L_);
    lua_pushcfunction(L_, [](lua_State* L) -> int {
        auto* d = static_cast<ClosureData*>(lua_touserdata(L, 1));
        if (d) d->~ClosureData();
        return 0;
    }, "__gc");
    lua_setfield(L_, -2, "__gc");
    lua_setmetatable(L_, -2);

    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!data)
        {
            luaL_error(L, "C++ callback data is unavailable");
            return 0;
        }

        int top = lua_gettop(L);
        std::vector<Value> args;
        args.reserve(top);
        for (int i = 1; i <= top; ++i)
        {
            args.push_back(Value::FromLuaState(L, i));
        }
        try
        {
            State vm(L);
            Value res = data->func(vm, args);
            if (lua_status(L) == LUA_YIELD)
                return lua_yield(L, 0);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& e)
        {
            luaL_error(L, "C++ callback exception: %s", e.what());
            return 0;
        }
        catch (...)
        {
            luaL_error(L, "C++ callback threw an unknown exception");
            return 0;
        }
    };

    // The userdata (at -1) is captured as the closure upvalue.
    lua_pushcclosure(L_, cfunc, "CFunction", 1);
    Value val = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return val;
}

Value State::CreateFastFunction(const std::function<Value(State& vm, StackArgs args)>& fn)
{
    if (!L_) return Value();

    struct ClosureData
    {
        std::function<Value(State& vm, StackArgs args)> func;
    };
    // GC-tracked userdata owns the payload: it is freed (destructor runs) when
    // the closure is collected, instead of leaking on every CreateFastFunction call.
    auto* data = static_cast<ClosureData*>(lua_newuserdata(L_, sizeof(ClosureData)));
    new (data) ClosureData{ fn };

    lua_newtable(L_);
    lua_pushcfunction(L_, [](lua_State* L) -> int {
        auto* d = static_cast<ClosureData*>(lua_touserdata(L, 1));
        if (d) d->~ClosureData();
        return 0;
    }, "__gc");
    lua_setfield(L_, -2, "__gc");
    lua_setmetatable(L_, -2);

    auto cfunc = [](lua_State* L) -> int {
        auto* data = static_cast<ClosureData*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!data)
        {
            luaL_error(L, "C++ callback data is unavailable");
            return 0;
        }

        StackArgs args(L);
        try
        {
            State vm(L);
            Value res = data->func(vm, args);
            if (lua_status(L) == LUA_YIELD)
                return lua_yield(L, 0);
            res.PushToLuaState(L);
            return 1;
        }
        catch (const std::exception& e)
        {
            luaL_error(L, "C++ callback exception: %s", e.what());
            return 0;
        }
        catch (...)
        {
            luaL_error(L, "C++ callback threw an unknown exception");
            return 0;
        }
    };

    // The userdata (at -1) is captured as the closure upvalue.
    lua_pushcclosure(L_, cfunc, "CFunctionFast", 1);
    Value val = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return val;
}

Coroutine State::CreateCoroutine(const Value& fn)
{
    if (!L_) return Coroutine();
    fn.PushToLuaState(L_);
    int fnRef = lua_ref(L_, -1);
    lua_pop(L_, 1);

    Coroutine co(L_, fnRef);
    lua_unref(L_, fnRef);
    return co;
}

void* State::CreateUserdata(size_t size)
{
    return L_ ? lua_newuserdata(L_, size) : nullptr;
}

Value State::CreateBuffer(size_t size)
{
    if (!L_) return Value();
    lua_newbuffer(L_, size);
    Value v = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return v;
}

void State::SetUserdataMetatable(int index, const Table& metatable)
{
    if (!L_) return;
    metatable.PushToLuaState(L_);
    lua_setmetatable(L_, index < 0 ? index - 1 : index);
}

void State::SetUserdataGC(const Table& metatable, void(*destructor)(void* ptr))
{
    if (!L_) return;
    metatable.PushToLuaState(L_);
    // Store the destructor in a full userdata (a real object pointer) and copy its
    // bytes with memcpy, instead of reinterpret_cast-ing the function pointer to
    // void*. Casting between function-pointer and object-pointer types is
    // conditionally-supported / implementation-defined, so the lightuserdata route
    // is not portable (see issue #12). The closure keeps the userdata alive as an
    // upvalue, and reads the destructor back out by copying the bytes.
    void* storage = lua_newuserdata(L_, sizeof(void(*)(void*)));
    std::memcpy(storage, &destructor, sizeof(destructor));
    auto gcFunc = [](lua_State* L) -> int {
        void (*dt)(void*) = nullptr;
        if (void* storage = lua_touserdata(L, lua_upvalueindex(1)))
            std::memcpy(&dt, storage, sizeof(dt));
        if (void* ud = lua_touserdata(L, 1))
            dt(ud);
        return 0;
    };
    lua_pushcclosure(L_, gcFunc, "__gc", 1);
    lua_setfield(L_, -2, "__gc");
    lua_pop(L_, 1);
}

int State::YieldThread()
{
    if (!L_) return 0;
    return lua_yield(L_, 0);
}

Value State::Require(std::string_view moduleName)
{
    // Mirrors Luau's built-in require(): if the module is not found or fails to load,
    // a Lua error is raised and propagates naturally through the call stack.
    // The caller does not need to check a Result or wrap the call in a pcall equivalent.
    // With no Lua state there is no error context to raise into, so return Nil instead
    // of passing a null lua_State* to luaL_error (which would dereference it and crash).
    if (!L_) return Value();
    lua_getglobal(L_, "require");
    if (!lua_isfunction(L_, -1))
    {
        lua_pop(L_, 1);
        luaL_error(L_, "Global require function is not available");
    }
    lua_pushlstring(L_, moduleName.data(), moduleName.size());
    // lua_call propagates errors via longjmp, identical to calling require() from Luau.
    lua_call(L_, 1, 1);
    Value val = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return val;
}

Result<Value> State::TryRequire(std::string_view moduleName)
{
    // Protected variant: catches any Lua error and returns it as a Result.
    // Use this when you want to inspect or recover from a missing module.
    if (!L_) return Error::Runtime("State is null");
    lua_getglobal(L_, "require");
    if (!lua_isfunction(L_, -1))
    {
        lua_pop(L_, 1);
        return Error::Runtime("Global require is missing");
    }
    lua_pushlstring(L_, moduleName.data(), moduleName.size());
    int status = lua_pcall(L_, 1, 1, 0);
    if (status != LUA_OK)
    {
        std::string errStr = LuaErrorMessage(L_, -1);
        lua_pop(L_, 1);
        return Error::Module("Require failed: " + errStr);
    }
    Value val = Value::FromLuaState(L_, -1);
    lua_pop(L_, 1);
    return val;
}

void State::RaiseError(std::string_view message)
{
    if (L_)
    {
        std::string msgStr(message);
        luaL_error(L_, "%s", msgStr.c_str());
    }
}

// --- Stack Manipulation API ---
int State::GetTop() const { return L_ ? lua_gettop(L_) : 0; }
void State::SetTop(int index) { if (L_) lua_settop(L_, index); }
void State::Pop(int count) { if (L_) lua_pop(L_, count); }
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
void State::PushValues(const std::vector<Value>& values)
{
    if (!L_) return;
    for (const auto& val : values)
    {
        val.PushToLuaState(L_);
    }
}
void State::PushTable(const Table& table) { if (L_) table.PushToLuaState(L_); }

// --- Stack Type Inspection API ---
bool State::IsNil(int index) const { return L_ ? (lua_isnil(L_, index) != 0) : false; }
bool State::IsBoolean(int index) const { return L_ ? (lua_isboolean(L_, index) != 0) : false; }
bool State::IsNumber(int index) const { return L_ ? (lua_isnumber(L_, index) != 0) : false; }
bool State::IsInteger(int index) const
{
    if (!L_) return false;
    if (lua_type(L_, index) == LUA_TINTEGER) return true;
    if (!lua_isnumber(L_, index)) return false;
    double d = lua_tonumber(L_, index);
    return std::isfinite(d) && d == std::trunc(d) && std::fabs(d) <= static_cast<double>(INT64_MAX);
}
bool State::IsString(int index) const { return L_ ? (lua_isstring(L_, index) != 0) : false; }
bool State::IsTable(int index) const { return L_ ? (lua_istable(L_, index) != 0) : false; }
bool State::IsFunction(int index) const { return L_ ? (lua_isfunction(L_, index) != 0) : false; }
bool State::IsThread(int index) const { return L_ ? (lua_isthread(L_, index) != 0) : false; }
bool State::IsUserdata(int index) const { return L_ ? (lua_isuserdata(L_, index) != 0) : false; }
bool State::IsLightUserdata(int index) const { return L_ ? (lua_islightuserdata(L_, index) != 0) : false; }
bool State::IsBuffer(int index) const { return L_ ? (lua_type(L_, index) == LUA_TBUFFER) : false; }

// --- Stack Reading API ---
Value State::GetValue(int index) const { return L_ ? Value::FromLuaState(L_, index) : Value(); }
std::string State::GetString(int index) const
{
    if (!L_) return "";
    size_t length = 0;
    const char* str = lua_tolstring(L_, index, &length);
    return str ? std::string(str, length) : std::string();
}
double State::GetNumber(int index) const { return L_ ? lua_tonumber(L_, index) : 0.0; }
int State::GetInteger(int index) const { return L_ ? static_cast<int>(lua_tonumber(L_, index)) : 0; }
bool State::GetBoolean(int index) const { return L_ ? (lua_toboolean(L_, index) != 0) : false; }
void* State::GetBuffer(int index, size_t* sizeOut) const { return (L_ && lua_type(L_, index) == LUA_TBUFFER) ? lua_tobuffer(L_, index, sizeOut) : (sizeOut ? (*sizeOut = 0, nullptr) : nullptr); }
void* State::GetUserdata(int index) const { return L_ ? lua_touserdata(L_, index) : nullptr; }
void* State::GetLightUserdata(int index) const { return L_ ? lua_touserdata(L_, index) : nullptr; }

std::span<uint8_t> State::GetBufferSpan(int index) const
{
    size_t size = 0;
    void* ptr = GetBuffer(index, &size);
    return ptr ? std::span<uint8_t>(static_cast<uint8_t*>(ptr), size) : std::span<uint8_t>();
}

std::string_view State::GetStringView(int index) const
{
    if (!L_ || !lua_isstring(L_, index)) return std::string_view();
    size_t len = 0;
    const char* str = lua_tolstring(L_, index, &len);
    return str ? std::string_view(str, len) : std::string_view();
}

// --- Stack Table & Field API ---
void State::GetField(int index, const char* name) { if (L_) lua_getfield(L_, index, name); }
void State::SetField(int index, const char* name) { if (L_) lua_setfield(L_, index, name); }
void State::RawGet(int index, int n) { if (L_) lua_rawgeti(L_, index, n); }
void State::RawSet(int index, int n) { if (L_) lua_rawseti(L_, index, n); }

} // namespace Lode
