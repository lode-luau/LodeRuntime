// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/State.hpp"
#include "Lode/Metatable.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/CFunctionCallContext.hpp"
#include "ModuleLoader.hpp"
#include "Registry.hpp"
#include "LuaError.hpp"
#include "NativeClosure.hpp"
#include "lua.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "uv.h"
#include "lualib.h"
#include "Luau/Compiler.h"
#include "Luau/CodeGen.h"
#include "StateLifetime.hpp"
#include <stdexcept>
#include <cmath>
#include <cstdint>

namespace Lode
{

struct State::Impl
{
    std::shared_ptr<Detail::StateLifetime> lifetime;
    NativeModuleRegistry registry;
    std::vector<std::string> modulePaths;
    std::unique_ptr<EventLoop> ownedEventLoop;
    EventLoop* eventLoop = nullptr;
    CodeGenMode codeGenMode = CodeGenMode::NativeModulesOnly;
};

namespace
{
// Process-unique addresses used as lightuserdata keys into each VM's
// registry. They cache the shared Impl / event-loop raw pointers so
// non-owning State views (one per native callback) can be built without
// touching the lifetime mutex/map or hashing string keys. Each VM has its
// own registry, so the same key is safe across runtimes.
char g_implCacheKey = 0;
char g_loopCacheKey = 0;

void RegistrySetPointer(lua_State* L, void* key, void* value)
{
    lua_pushlightuserdata(L, key);
    lua_pushlightuserdata(L, value);
    lua_settable(L, LUA_REGISTRYINDEX);
}

void* RegistryGetPointer(lua_State* L, void* key)
{
    lua_pushlightuserdata(L, key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    void* result = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return result;
}
} // namespace

State::State() : L_(luaL_newstate()), ownsState_(true), impl_(std::make_unique<Impl>())
{
    if (L_)
    {
        impl_->lifetime = Detail::RegisterStateLifetime(L_);
        // Publish this root Impl so every non-owning State view (native
        // callbacks, coroutines) shares it instead of building its own.
        if (impl_->lifetime)
            impl_->lifetime->sharedImpl = impl_;
        try
        {
            impl_->ownedEventLoop = std::make_unique<EventLoop>();
        }
        catch (const std::exception&)
        {
            // An unusable event loop makes async operations fail silently, so a
            // State that cannot own a loop must not be handed out. Release the
            // lua_State before rethrowing so the caller sees a clean error.
            Detail::InvalidateStateLifetime(L_);
            lua_close(L_);
            L_ = nullptr;
            throw;
        }
        impl_->eventLoop = impl_->ownedEventLoop.get();
        lua_pushlightuserdata(L_, impl_->eventLoop);
        lua_setfield(L_, LUA_REGISTRYINDEX, "_LODE_EVENT_LOOP");
        // Publish O(1)-lookup pointers for non-owning State views.
        RegistrySetPointer(L_, &g_implCacheKey, impl_.get());
        RegistrySetPointer(L_, &g_loopCacheKey, impl_->eventLoop);
        luaL_openlibs(L_);
        SetupModuleLoader(L_, &impl_->registry, impl_->modulePaths);

        if (Luau::CodeGen::isSupported())
        {
            Luau::CodeGen::create(L_);
        }
    }
}

State::State(lua_State* L) : L_(L), ownsState_(false)
{
    if (!L_)
        return;

    // Fast path: the root VM published raw pointers in its registry, so
    // building a view is just two table lookups — no mutex, no map lookup,
    // no heap allocation. The Impl is owned by the root's StateLifetime
    // slot; this view holds a non-owning shared_ptr, valid for as long as
    // the VM is alive (a precondition of any callback running on it).
    // NOTE: the Impl allocation must stay out of the init list, otherwise
    // it runs unconditionally before this branch replaces it.
    auto* cachedImpl = static_cast<Impl*>(RegistryGetPointer(L_, &g_implCacheKey));
    if (cachedImpl)
    {
        impl_ = std::shared_ptr<Impl>(std::shared_ptr<void>(), cachedImpl);
        if (auto* cachedLoop = static_cast<EventLoop*>(RegistryGetPointer(L_, &g_loopCacheKey)))
            impl_->eventLoop = cachedLoop;
        return;
    }

    // Slow path (views created before/without a publishing root): share via
    // the lifetime map, or build a throwaway Impl as a last resort.
    auto lifetime = Detail::GetStateLifetime(L_);
    if (lifetime && lifetime->sharedImpl)
    {
        impl_ = std::static_pointer_cast<Impl>(lifetime->sharedImpl);
    }
    else
    {
        impl_ = std::make_shared<Impl>();
        if (lifetime)
            lifetime->sharedImpl = impl_;
    }
    impl_->lifetime = lifetime;
    lua_getfield(L_, LUA_REGISTRYINDEX, "_LODE_EVENT_LOOP");
    impl_->eventLoop = static_cast<EventLoop*>(lua_touserdata(L_, -1));
    lua_pop(L_, 1);
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

namespace
{
    // Cancels pending async work owned by this State (timers, event loop).
    void CloseAsyncResources(State& vm, EventLoop* eventLoop)
    {
        Task::Shutdown(vm);
        if (eventLoop)
            eventLoop->Close();
    }
}

State::~State()
{
    if (ownsState_ && L_)
    {
        // Cancel every pending timer for this State before the VM is closed.
        // Timers hold Value/Coroutine references to this lua_State, so they
        // must be released while the state is still open (see issue #9).
        CloseAsyncResources(*this, impl_ ? impl_->ownedEventLoop.get() : nullptr);
        Detail::InvalidateStateLifetime(L_);
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
            CloseAsyncResources(*this, impl_ ? impl_->ownedEventLoop.get() : nullptr);
            Detail::InvalidateStateLifetime(L_);
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
    try
    {
        State s;
        if (!s.L_)
        {
            return Error::Platform("Failed to initialize Luau VM state");
        }
        return s;
    }
    catch (const std::exception& e)
    {
        return Error::Platform(std::string("Failed to initialize runtime state: ") + e.what());
    }
}

Result<void> State::ExecuteBytecode(std::string_view bytecode, std::string_view chunkName)
{
    auto res = ExecuteBytecodeWithResults(bytecode, chunkName, true);
    if (res.IsError()) return res.GetError();
    return Result<void>();
}

namespace Detail
{
thread_local lua_State* g_moduleLoadCo = nullptr;
thread_local std::vector<Value>* g_moduleLoadSink = nullptr;
}

Result<int> State::ExecuteBytecodeWithResults(std::string_view bytecode, std::string_view chunkName, bool isMainScript, bool errorOnYield)
{
    if (!L_) return Error::Runtime("State is null");
    const int baseTop = lua_gettop(L_);

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

    // Native code generation honors the per-State mode set via SetCodeGenMode
    // (CLI --codegen): off skips compilation entirely, all-functions passes no
    // restriction flag so every function is compiled, and the default mirrors
    // the historical behavior of compiling only --!native modules.
    const CodeGenMode codeGenMode = impl_ ? impl_->codeGenMode : CodeGenMode::NativeModulesOnly;
    if (codeGenMode != CodeGenMode::Off && Luau::CodeGen::isSupported())
    {
        if (codeGenMode == CodeGenMode::AllFunctions)
            Luau::CodeGen::compile(co, -1, 0);
        else
            Luau::CodeGen::compile(co, -1, Luau::CodeGen::CodeGen_OnlyNativeModules);
    }

    // Arm module-load capture: ResumeCore hands this thread's return values
    // to the sink instead of dropping them when they complete asynchronously.
    std::vector<Value> loadSink;
    std::vector<Value>* prevSink = Detail::g_moduleLoadSink;
    lua_State* prevCo = Detail::g_moduleLoadCo;
    Detail::g_moduleLoadCo = co;
    Detail::g_moduleLoadSink = &loadSink;

    int resStatus = lua_resume(co, nullptr, 0);

    // Module-load mode (errorOnYield): a module may legitimately yield at
    // top level by calling async fs/net natives. Instead of failing or
    // silently resolving to nil, pump the event loop so those operations
    // complete and the module coroutine finishes, bounded by a hard 5s
    // timeout after which we fail loudly.
    if (resStatus == LUA_YIELD && errorOnYield)
    {
        EventLoop* loop = impl_ ? impl_->eventLoop : nullptr;
        if (!loop)
        {
            lua_unref(L_, coRef);
            return Error::Runtime("Module yielded during require but no event loop is available");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (lua_status(co) == LUA_YIELD)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                lua_unref(L_, coRef);
                return Error::Runtime("Module timed out after 5 seconds during require — an async call at module top-level never completed");
            }
            uv_run(loop->GetUVLoop(), UV_RUN_ONCE);
        }
        // Results were captured by ResumeCore into the sink: push them onto
        // L_ so the require boundary receives the module's exports.
        for (auto& v : loadSink)
            v.PushToLuaState(L_);
        lua_unref(L_, coRef);
        Detail::g_moduleLoadSink = prevSink;
        Detail::g_moduleLoadCo = prevCo;
        return static_cast<int>(loadSink.size());
    }

    // Non-yield path (sync completion or plain error): restore capture state.
    Detail::g_moduleLoadSink = prevSink;
    Detail::g_moduleLoadCo = prevCo;

    if (resStatus != LUA_OK && resStatus != LUA_YIELD)
    {
        const char* msg = lua_tostring(co, -1);
        lua_rawcheckstack(co, 2);
        luaL_traceback(co, co, msg, 1);
        std::string errStr = LuaErrorMessage(co, -1);
        lua_pop(co, 2);
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

void State::SetStandardLibraryPath(std::string_view path)
{
    if (!L_) return;
    Lode::SetStandardLibraryPath(L_, path);
}

void State::SetCliArgs(const std::vector<std::string>& args)
{
    if (!L_) return;
    lua_createtable(L_, static_cast<int>(args.size()), 0);
    for (size_t i = 0; i < args.size(); ++i)
    {
        lua_pushlstring(L_, args[i].data(), args[i].size());
        lua_rawseti(L_, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L_, LUA_REGISTRYINDEX, "_LODE_CLI_ARGS");
}

std::vector<std::string> State::GetCliArgs() const
{
    std::vector<std::string> result;
    if (!L_) return result;
    lua_getfield(L_, LUA_REGISTRYINDEX, "_LODE_CLI_ARGS");
    if (lua_istable(L_, -1))
    {
        int len = lua_objlen(L_, -1);
        for (int i = 1; i <= len; ++i)
        {
            lua_rawgeti(L_, -1, i);
            if (lua_isstring(L_, -1))
            {
                size_t sz = 0;
                const char* s = lua_tolstring(L_, -1, &sz);
                result.emplace_back(s, sz);
            }
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
    return result;
}

void State::SetCodeGenMode(CodeGenMode mode)
{
    if (!impl_) return;
    impl_->codeGenMode = mode;
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
    return Detail::CreateClosure(L_, "CFunction", [fn](State& vm, lua_State* L) -> Value {
        int top = lua_gettop(L);
        std::vector<Value> args;
        args.reserve(top);
        for (int i = 1; i <= top; ++i)
        {
            args.push_back(Value::FromLuaState(L, i));
        }
        return fn(vm, args);
    });
}

Value State::CreateFastFunction(const std::function<Value(State& vm, StackArgs args)>& fn)
{
    if (!L_) return Value();
    return Detail::CreateClosure(L_, "CFunctionFast", [fn](State& vm, lua_State* L) -> Value {
        return fn(vm, StackArgs(L));
    });
}

Value State::CreateFastFunctionN(const std::function<int(State& vm, StackArgs args)>& fn)
{
    if (!L_) return Value();
    return Detail::CreateClosureN(L_, "CFunctionFastN", [fn](State& vm, lua_State* L) -> int {
        return fn(vm, StackArgs(L));
    });
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

void* State::CreateUserdata(size_t size, void(*destructor)(void* ptr))
{
    return L_ ? lua_newuserdatadtor(L_, size, destructor) : nullptr;
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

void State::FreezeTable(const Table& table)
{
    if (!L_ || !table.GetLuaState()) return;
    table.PushToLuaState(L_);
    lua_setreadonly(L_, -1, true);
    Pop(1);
}

void State::SetUserdataGC(const Table& metatable, void(*destructor)(void* ptr))
{
    (void)metatable;
    (void)destructor;
}

int State::YieldThread()
{
    if (!L_) return 0;
    const auto& execution = Detail::CurrentCFunctionCallContext();
    if (execution.inForeignCallback && !execution.callbackMayYield)
    {
        luaL_error(L_, "cannot yield from a synchronous foreign callback");
        return 0;
    }
    Detail::CurrentCFunctionCallContext().explicitYieldRequested = true;
    return lua_yield(L_, 0);
}

namespace
{
    // Pushes the require global and the module name; returns false when the
    // global is missing (leaving the stack balanced).
    bool PushRequireCall(lua_State* L, std::string_view moduleName)
    {
        lua_getglobal(L, "require");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        lua_pushlstring(L, moduleName.data(), moduleName.size());
        return true;
    }
}

Value State::Require(std::string_view moduleName)
{
    // Mirrors Luau's built-in require(): if the module is not found or fails to load,
    // a Lua error is raised and propagates naturally through the call stack.
    // The caller does not need to check a Result or wrap the call in a pcall equivalent.
    // With no Lua state there is no error context to raise into, so return Nil instead
    // of passing a null lua_State* to luaL_error (which would dereference it and crash).
    if (!L_) return Value();
    if (!PushRequireCall(L_, moduleName))
        luaL_error(L_, "Global require function is not available");
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
    if (!PushRequireCall(L_, moduleName))
        return Error::Runtime("Global require is missing");
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
