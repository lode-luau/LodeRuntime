// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Signal.hpp"
#include "Lode/Coroutine.hpp"
#include "Sys/SysExport.hpp"
#include <uv.h>
#include <memory>
#include <string>
#include <vector>
#include <deque>

namespace lodesys
{

struct ProcessManager;

struct SYS_API ProcessPipeStream : std::enable_shared_from_this<ProcessPipeStream>
{
    std::shared_ptr<ProcessManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    uv_pipe_t pipe{};

    bool isReadable = false;
    bool pipeInited = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;

    std::shared_ptr<Lode::Signal> dataSig;
    std::shared_ptr<Lode::Signal> endSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value dataProxy;
    Lode::Value endProxy;
    Lode::Value errorProxy;

    std::vector<uint8_t> readBuffer;

    struct PendingRead
    {
        bool isLine = false;
        bool isBuffer = false;
        size_t requestedBytes = 0;
        Lode::Coroutine coroutine;
    };

    std::deque<PendingRead> pendingQueue;
    std::shared_ptr<ProcessPipeStream> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void StartReading();
    void StopReading();
    void WriteNative(const char* data, size_t size);

    void ProcessQueue(bool isEof = false);
    bool TryResolve(Lode::State& vm, const PendingRead& req, Lode::Value& outVal);

    Lode::Value MethodWrite(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodWriteLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodRead(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadBuffer(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodStartStreaming(Lode::State& vm);
    Lode::Value MethodStopStreaming(Lode::State& vm);

    void RequestClose();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

struct SYS_API Process : std::enable_shared_from_this<Process>
{
    std::shared_ptr<ProcessManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_process_t process{};
    uv_process_options_t options{};

    int pid = 0;
    bool running = false;
    bool exited = false;
    int64_t exitCode = -1;
    int termSignal = 0;
    bool handleClosed = false;

    std::shared_ptr<ProcessPipeStream> stdinStream;
    std::shared_ptr<ProcessPipeStream> stdoutStream;
    std::shared_ptr<ProcessPipeStream> stderrStream;

    std::shared_ptr<Lode::Signal> exitSig;
    std::shared_ptr<Lode::Signal> errorSig;
    Lode::Value exitProxy;
    Lode::Value errorProxy;

    std::vector<Lode::Coroutine> waitCoroutines;
    std::shared_ptr<Process> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void OnExit(int64_t exit_status, int term_signal);
    void RequestClose();

    Lode::Value MethodKill(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodWait(Lode::State& vm);
    Lode::Value MethodGetPid(Lode::State& vm);
    Lode::Value MethodGetExitCode(Lode::State& vm);
    Lode::Value MethodGetTermSignal(Lode::State& vm);
    Lode::Value MethodIsRunning(Lode::State& vm);
    Lode::Value MethodClose(Lode::State& vm);

    static void OnProcessExit(uv_process_t* req, int64_t exit_status, int term_signal);
    static void OnProcessClosed(uv_handle_t* handle);
};

struct SYS_API ProcessManager : std::enable_shared_from_this<ProcessManager>
{
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;
    bool shuttingDown = false;

    Lode::Table processMethods;
    Lode::Table streamMethods;

    std::vector<std::shared_ptr<Process>> processes;
    std::vector<std::shared_ptr<ProcessPipeStream>> streams;

    void AddProcess(const std::shared_ptr<Process>& p);
    void RemoveProcess(const std::shared_ptr<Process>& p);

    void AddStream(const std::shared_ptr<ProcessPipeStream>& s);
    void RemoveStream(const std::shared_ptr<ProcessPipeStream>& s);

    void Shutdown();
};

SYS_API Lode::Value WrapProcess(Lode::State& vm, const std::shared_ptr<Process>& p, const Lode::Table& methods);
SYS_API Lode::Value WrapProcessStream(Lode::State& vm, const std::shared_ptr<ProcessPipeStream>& s, const Lode::Table& methods);

// =======================================================
// Native C++ APIs (Exported for other modules)
// =======================================================
SYS_API std::string GuessHandleType(int fd);

// =======================================================
// Luau Bindings
// =======================================================
SYS_API void BindSysProcess(Lode::State& vm, Lode::Table& exports, const std::shared_ptr<ProcessManager>& mgr);

} // namespace lodesys
