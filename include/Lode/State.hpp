// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/StackValue.hpp"
#include "lua.h"
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <memory>
#include <functional>
#include <utility>

struct lua_State;

namespace Lode
{

class Metatable;
class EventLoop;

/**
 * @brief Selects which functions Luau's native code generation compiles when
 * bytecode executes.
 */
enum class CodeGenMode
{
    NativeModulesOnly, ///< Compile only functions from modules marked --!native (default).
    AllFunctions,      ///< Compile every function regardless of module directives.
    Off                ///< Skip native code generation; everything stays interpreted.
};

/**
 * @brief Represents an isolated Luau virtual machine instance or a thread state.
 * 
 * The State class is the central point of execution in Lode Runtime. It manages
 * the lua_State pointer, module resolution, globals, and C++ to Luau bindings.
 */
class LODE_API State
{
public:
    State();
    explicit State(lua_State* L);
    State(lua_State* L, bool nativeYieldAllowed);
    ~State();

    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&& other) noexcept;
    State& operator=(State&& other) noexcept;

    /**
     * @brief Creates a new, fully initialized State with standard libraries.
     * @return Result containing the State on success, or an Error on failure.
     */
    static Result<State> Create();

    /**
     * @brief Executes compiled Luau bytecode.
     * @param bytecode The compiled Luau bytecode.
     * @param chunkName The name of the chunk (used for error reporting).
     * @return Result indicating success or failure.
     */
    Result<void> ExecuteBytecode(std::string_view bytecode, std::string_view chunkName = "=main");

    /**
     * @brief Executes compiled Luau bytecode and returns the number of results pushed to the stack.
     * @param bytecode The compiled Luau bytecode.
     * @param chunkName The name of the chunk.
     * @param isMainScript Set to true when executing the top-level script (not a module), so its
     * thread is registered as the main thread for fatal-error reporting.
     * @return Result containing the number of returned values.
     */
    Result<int> ExecuteBytecodeWithResults(std::string_view bytecode, std::string_view chunkName = "=main", bool isMainScript = false, bool errorOnYield = false);

    /**
     * @brief Executes bytecode and captures the first returned value.
     * @param bytecode The compiled Luau bytecode.
     * @param chunkName The name of the chunk.
     * @return Result containing the returned Value.
     */
    Result<Value> ProtectedCall(std::string_view bytecode, std::string_view chunkName = "=main");

    /**
     * @brief Calls a Luau function safely.
     * @param fn The Luau function to call.
     * @param args Arguments to pass; each is converted to a Value. A std::vector<Value>
     *             passed as a single argument is spread as multiple arguments.
     * @return Result containing all returned Values.
     */
    template <typename... Args>
    Result<std::vector<Value>> CallFunction(const Value& fn, Args&&... args)
    {
        return fn.Call(*this, std::forward<Args>(args)...);
    }

    void AddModulePath(std::string_view path);

    /** @brief Sets the runtime-owned standard library root used as a module fallback. */
    void SetStandardLibraryPath(std::string_view path);

    void SetCliArgs(const std::vector<std::string>& args);
    [[nodiscard]] std::vector<std::string> GetCliArgs() const;

    /** @brief Sets which functions native code generation compiles at execution time (default NativeModulesOnly). */
    void SetCodeGenMode(CodeGenMode mode);

    void SetGlobal(const std::string& name, const Value& value);
    [[nodiscard]] Result<Value> GetGlobal(const std::string& name) const;

    /**
     * @brief Creates a new empty Luau Table.
     * @return The created Table.
     */
    [[nodiscard]] Table CreateTable();

    /**
     * @brief Creates a new empty Metatable.
     * @return The created Metatable.
     */
    [[nodiscard]] Metatable CreateMetatable();

    /**
     * @brief Creates a Luau function bound to a C++ lambda (Standard).
     * @param fn The C++ lambda to bind.
     * @return The created Function Value.
     */
    [[nodiscard]] Value CreateFunction(const std::function<Value(State& vm, const std::vector<Value>& args)>& fn);

    /**
     * @brief Creates a fast, zero-allocation Luau function bound to a C++ lambda.
     * @param fn The C++ lambda to bind, using StackArgs for zero-heap overhead.
     * @return The created Function Value.
     */
    [[nodiscard]] Value CreateFastFunction(const std::function<Value(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates a fast native function that is explicitly non-yieldable.
     *
     * This is the lowest-overhead Value-returning native wrapper. It does not
     * inspect Lua status or the scheduler; calling YieldThread from it is an
     * error. C++ exceptions are still converted to Lua errors.
     */
    [[nodiscard]] Value CreateFastFunctionNoYield(const std::function<Value(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates a fast, zero-allocation Luau function with zero-marshaling results.
     *
     * The bound callable reads its arguments through StackArgs (no Lode::Value
     * boxing) AND pushes its own return values directly onto the Lua stack,
     * returning their count. Compared to CreateFastFunction this removes the
     * final Value boxing of the result and enables multi-value returns. Use
     * for hot-path native functions; the callable must leave exactly the
     * returned number of new values on the stack.
     * @param fn The C++ lambda: int(State& vm, StackArgs args), pushing nresults.
     * @return The created Function Value.
     */
    [[nodiscard]] Value CreateFastFunctionN(const std::function<int(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates a direct-stack native function with no yield checks.
     *
     * The callable receives a lightweight State view bound to the active
     * coroutine and must not call YieldThread, Task::Wait, or another
     * yieldable operation. It pushes results directly to the raw Lua stack
     * and returns their count. Contract errors and C++ exceptions remain
     * capturable through pcall; native memory faults remain native faults.
     */
    [[nodiscard]] Value CreateFastFunctionNNoYield(const std::function<int(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates an explicitly yieldable Value-returning native function.
     * @note YieldThread is the only operation that may suspend the coroutine.
     */
    [[nodiscard]] Value CreateFastFunctionYieldable(const std::function<Value(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates an explicitly yieldable direct-stack native function.
     * @note The callable must return the number of values it pushed after
     *       resumption; ordinary synchronous functions should use NNoYield.
     */
    [[nodiscard]] Value CreateFastFunctionNYieldable(const std::function<int(State& vm, StackArgs args)>& fn);

    /**
     * @brief Creates a new coroutine (thread) from a function.
     * @param fn The function to run in the coroutine.
     * @return The created Coroutine.
     */
    [[nodiscard]] Coroutine CreateCoroutine(const Value& fn);

    /**
     * @brief Allocates userdata memory managed by Luau's Garbage Collector.
     * @param size The size of the userdata in bytes.
     * @return Pointer to the allocated memory.
     */
    void* CreateUserdata(size_t size);
    void* CreateUserdata(size_t size, void(*destructor)(void* ptr));

    /**
     * @brief Creates a Luau buffer of the specified size.
     * @param size The size of the buffer in bytes.
     * @return The created Buffer Value.
     */
    [[nodiscard]] Value CreateBuffer(size_t size);

    void SetUserdataMetatable(int index, const Table& metatable);
    /** @deprecated Luau metatables do not run __gc; pass a destructor to CreateUserdata instead. */
    void SetUserdataGC(const Table& metatable, void(*destructor)(void* ptr));

    /** @brief Marks a table as read-only (frozen) so Luau cannot modify it. */
    void FreezeTable(const Table& table);

    /**
     * @brief Yields the current executing Luau coroutine.
     * 
     * @warning **CRITICAL C++ INTERACTION**: This method internally calls `lua_yield`, 
     * which typically performs a `longjmp` or throws a C++ exception depending on the 
     * Luau compilation flags. This means that **execution will NOT return to the caller**.
     * Any C++ code below this function call will be skipped. Ensure all local objects 
     * with important destructors (e.g. locks, smart pointers) are destructed BEFORE 
     * calling this method.
     * 
     * @return Never returns natively.
     */
    int YieldThread();

    // Require a module — raises a Lua error on failure, identical to Luau's built-in require().
    // Use this when you want the error to propagate naturally (no wrapping needed).
    // If the State has no Lua thread (null), it cannot raise into the VM, so it returns Nil.
    Value Require(std::string_view moduleName);

    // Require a module without throwing — returns Result<Value> for explicit error handling.
    Result<Value> TryRequire(std::string_view moduleName);

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
    [[nodiscard]] bool IsBuffer(int index = -1) const;

    // --- Stack Reading API ---
    [[nodiscard]] Value GetValue(int index) const;
    [[nodiscard]] std::string GetString(int index) const;
    [[nodiscard]] double GetNumber(int index) const;
    [[nodiscard]] int GetInteger(int index) const;
    [[nodiscard]] bool GetBoolean(int index) const;
    [[nodiscard]] void* GetBuffer(int index = -1, size_t* sizeOut = nullptr) const;
    [[nodiscard]] void* GetUserdata(int index = -1) const;
    [[nodiscard]] void* GetLightUserdata(int index = -1) const;
    [[nodiscard]] std::span<uint8_t> GetBufferSpan(int index = -1) const;
    [[nodiscard]] std::string_view GetStringView(int index = -1) const;

    // --- Stack Table & Field API ---
    void GetField(int index, const char* name);
    void SetField(int index, const char* name);
    void RawGet(int index, int n);
    void RawSet(int index, int n);

    /**
     * @brief Returns the current thread being executed.
     *
     * @warning The returned thread is a coroutine that the GC can collect once
     * it is no longer referenced. Do NOT store it across yields or use it from
     * async callbacks — use GetMainThread() for a GC-safe pointer instead.
     */
    [[nodiscard]] lua_State* GetLuaState() const { return L_; }

    /**
     * @brief Returns the VM's root thread, which is anchored and never collected
     * by the GC until the State is destroyed.
     *
     * This is the safe pointer to keep for native async callbacks, timers, and
     * module-long-lived state: it is always valid for lua_getref/lua_unref and
     * all registry operations while the State is alive.
     */
    [[nodiscard]] lua_State* GetMainThread() const;

    /** @brief Returns the event loop owned by this State. */
    [[nodiscard]] EventLoop& GetEventLoop() const;

private:
    struct Impl;
    lua_State* L_ = nullptr;
    bool ownsState_ = true;
    bool nativeYieldAllowed_ = true;
    std::shared_ptr<Impl> impl_;
};

} // namespace Lode
