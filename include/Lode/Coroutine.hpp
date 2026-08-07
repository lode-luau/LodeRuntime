// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include <vector>
#include <memory>

struct lua_State;

namespace Lode
{

namespace Detail { struct PinnedRef; }

/**
 * @brief Represents the execution status of a Coroutine (Thread).
 */
enum class CoroutineStatus
{
    Running,
    Suspended,
    Normal,
    Dead,
    Error
};

/**
 * @brief Represents a Luau thread (coroutine) managed by Lode.
 * 
 * Allows resuming and querying the status of asynchronous Luau tasks.
 */
class LODE_API Coroutine
{
public:
    /** @brief Constructs an empty, invalid Coroutine. */
    Coroutine();
    /** @brief Internal constructor for capturing a thread from a function reference. */
    Coroutine(lua_State* L, int fnRef);
    /** @brief Constructs a new coroutine from a given function Value. */
    Coroutine(State& vm, const Value& fn);
    /** @brief Wraps an existing raw lua_State thread. */
    explicit Coroutine(lua_State* threadState);
    ~Coroutine();

    Coroutine(const Coroutine& other);
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other);
    Coroutine& operator=(Coroutine&& other) noexcept;

    /** 
     * @brief Resumes the execution of the coroutine.
     * @param args Arguments to pass to the yielding function.
     * @return Result containing yielded/returned values on success, or an Error.
     */
    Result<std::vector<Value>> Resume(const std::vector<Value>& args = {});
    
    /** 
     * @brief Resumes the coroutine by throwing a Luau error natively.
     * @param errorMsg The error message to throw.
     * @return Result containing yielded/returned values on success, or an Error.
     */
    Result<std::vector<Value>> ResumeError(const std::string& errorMsg);
    
    /** @brief Queries the current state of the coroutine (e.g., Suspended, Dead). */
    [[nodiscard]] CoroutineStatus GetStatus() const;
    /** @brief Checks if the Coroutine is initialized and holds a valid thread. */
    [[nodiscard]] bool IsValid() const;

    /** @brief Retrieves the underlying raw thread pointer (lua_State*). */
    [[nodiscard]] lua_State* GetThreadState() const;

private:
    std::shared_ptr<Detail::PinnedRef> refData_;
};

} // namespace Lode
