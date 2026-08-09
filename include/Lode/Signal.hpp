// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/Export.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "Lode/Coroutine.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace Lode
{

/**
 * @brief An event emitter (observer pattern) for native modules.
 *
 * Lode::Signal mirrors the @signal module from modules/signal, but is created,
 * fired, and disposed entirely from C++ using only the public Lode API.
 *
 * Native modules hold a shared_ptr<Signal> and have full access: Connect, Once,
 * Fire, Disconnect, DisconnectAll. What gets exposed to Luau is a frozen proxy
 * table (see CreatePublic) that only offers Connect/Once/Wait — the same public
 * interface as modules/signal's Signal:Public(). The proxy is read-only: Luau
 * cannot add, remove, or replace fields (not even via rawset), and never sees
 * Fire.
 *
 * Lifecycle: the Signal registers a shutdown hook on its owning State, so all
 * pending waiters, timers, and connections are torn down before the State's
 * lua_State is closed. After shutdown, Fire/Connect/Wait become no-ops (Wait
 * raises). All methods must be called on the State's event loop thread, the
 * same thread that owns the VM (Lode's threading model).
 *
 * Fire args can be any Lode::Value. Callbacks run on the loop thread via
 * Lode::Task::Spawn; errors inside a fired callback surface as TaskError
 * diagnostics, matching the runtime's handling of async task errors. A signal
 * only fires while it owns the listener's callback, exactly like modules/signal.
 *
 * Usage:
 * ```cpp
 * auto tick = Lode::Signal::Create(vm);
 * exports.Set("onTick", tick->CreatePublic());
 * ...
 * tick->Fire({ Lode::Value(std::string("hello")) });
 * ```
 */
class LODE_API Signal : public std::enable_shared_from_this<Signal>
{
public:
    /**
     * @brief Creates a new Signal owned by the given State.
     *
     * The signal is reference-counted. Register a shutdown hook on the State so
     * pending resources are released before the State is destroyed.
     * @param vm The state the signal lives in.
     * @return The new Signal.
     */
    static std::shared_ptr<Signal> Create(State& vm);

    ~Signal();

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    // --- Native-side API (full access) ---

    /**
     * @brief Registers a callback that fires on every Fire.
     * @param callback The Luau function to call.
     * @return The connection id (0 if the callback was invalid).
     */
    uint64_t Connect(const Value& callback);

    /**
     * @brief Registers a one-shot callback that disconnects after the first Fire.
     * @param callback The Luau function to call.
     * @return The connection id (0 if the callback was invalid).
     */
    uint64_t Once(const Value& callback);

    /** @brief Disconnects a connection by id (no-op for unknown ids). */
    void Disconnect(uint64_t connectionId);

    /** @brief Disconnects every callback and waiter. */
    void DisconnectAll();

    /** @brief Checks whether a connection id is still active. */
    [[nodiscard]] bool IsConnected(uint64_t connectionId) const;

    /** @brief Returns the number of active connections (callbacks and waiters). */
    [[nodiscard]] size_t ConnectionCount() const;

    /** @brief Fires the signal with no arguments. */
    void Fire();

    /** @brief Fires the signal with a single argument. */
    void Fire(const Value& arg);

    /** @brief Fires the signal, calling all connected callbacks and resuming waiters. */
    void Fire(const std::vector<Value>& args);

    // --- Luau-facing API (read-only view) ---

    /**
     * @brief Returns a frozen proxy table with only Connect/Once/Wait.
     *
     * Equivalent to modules/signal's Signal:Public(). The proxy is a table
     * (so type(sig) == "table") marked read-only via State::FreezeTable. Luau
     * can connect, wait, and once, but cannot Fire, disconnect others, or
     * modify the proxy in any way.
     * @return The frozen proxy table.
     */
    Value CreatePublic();

    /**
     * @brief Yields the current thread until the signal fires or the timeout elapses.
     *
     * Mirrors modules/signal's SignalMT:Wait. args[0] is the optional timeout
     * in seconds (nil waits forever). On Fire the thread resumes with the fire
     * arguments; on timeout it resumes with nothing (nil). Errors raised by the
     * resumed continuation are routed to the main-thread error slot or to a
     * TaskError diagnostic, never swallowed.
     * @param vm The state executing the Wait call.
     * @param args The Wait arguments (timeout seconds, optional).
     * @return The value count from the VM yield (used by the VM, not callers).
     */
    int Wait(State& vm, const std::vector<Value>& args);

private:
    struct Connection;
    struct Waiter;
    struct Impl;

    explicit Signal(State& vm);

    uint64_t AddConnection(const Value& callback, bool once);
    void Shutdown();
    Value WrapConnection(State& vm, uint64_t connectionId);
    Value MakeResumeClosure(State& vm, const std::shared_ptr<Waiter>& waiter, const std::vector<Value>& args);

    std::unique_ptr<Impl> impl_;
};

} // namespace Lode
